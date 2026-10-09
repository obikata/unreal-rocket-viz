// URV wire protocol v1 decoder (Docs/wire-protocol.md).
//
// Deliberately free of Unreal types: plain C++17 and the standard library, so
// it is unit-tested outside the engine (Tests/wire_test.cpp, golden packets in
// Tests/golden/) and can be reused by any C++ receiver. AUrvUdpReceiver turns
// the decoded messages into FUrvFrame / FUrvEvent.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace UrvWire
{
constexpr uint32_t kMagic = 0x31565255u;   // "URV1"
constexpr uint16_t kVersion = 1;
constexpr std::size_t kHeaderBytes = 20;
constexpr const char* kDefaultGroup = "239.255.76.86";
constexpr uint16_t kDefaultPort = 47686;

enum class EType : uint16_t { Frame = 1, Event = 2, Scene = 3 };

struct FHeader
{
	uint32_t Magic = 0;
	uint16_t Version = 0;
	uint16_t Type = 0;
	uint32_t Seq = 0;
	uint32_t SenderId = 0;
	uint32_t PayloadBytes = 0;
};

struct FEntity
{
	int32_t Id = 0;
	double Pos[3] = {0, 0, 0};          // ECEF [m]
	double Vel[3] = {0, 0, 0};          // ECEF [m/s]
	double Q[4] = {1, 0, 0, 0};         // w, x, y, z: body -> ECEF, body +X = nose
	uint32_t EngineMask = 0;
	uint32_t Flags = 0;                 // bit 0: engine on
	std::vector<std::pair<std::string, double>> Channels;
};

struct FMessage
{
	FHeader Header;
	// FRAME
	double SimTime = 0.0;
	double SendTime = 0.0;
	std::vector<FEntity> Entities;
	// EVENT (SimTime as above)
	uint32_t EventId = 0;
	std::string EventName;
	// SCENE
	std::string SceneJson;
};

namespace Detail
{
class FReader
{
public:
	FReader(const uint8_t* InData, std::size_t InSize) : Data(InData), Size(InSize) {}

	template <typename T>
	bool Read(T& Out)
	{
		static_assert(std::is_trivially_copyable<T>::value, "POD only");
		if (Pos + sizeof(T) > Size) return false;
		std::memcpy(&Out, Data + Pos, sizeof(T));   // the wire and every Unreal target are little-endian
		Pos += sizeof(T);
		return true;
	}
	bool ReadString(std::string& Out)
	{
		uint8_t N = 0;
		if (!Read(N) || Pos + N > Size) return false;
		Out.assign(reinterpret_cast<const char*>(Data + Pos), N);
		Pos += N;
		return true;
	}
	bool ReadRest(std::string& Out)
	{
		Out.assign(reinterpret_cast<const char*>(Data + Pos), Size - Pos);
		Pos = Size;
		return true;
	}
	bool AtEnd() const { return Pos == Size; }

private:
	const uint8_t* Data;
	std::size_t Size;
	std::size_t Pos = 0;
};

inline bool LittleEndianHost()
{
	const uint16_t One = 1;
	uint8_t B = 0;
	std::memcpy(&B, &One, 1);
	return B == 1;
}
}  // namespace Detail

// Decodes one datagram. Returns false (and sets Error) for anything malformed;
// Out is then unspecified.
inline bool Decode(const uint8_t* Data, std::size_t Size, FMessage& Out, std::string* Error = nullptr)
{
	auto Fail = [Error](const char* Why) {
		if (Error) *Error = Why;
		return false;
	};
	if (!Detail::LittleEndianHost()) return Fail("big-endian host not supported");
	Detail::FReader R(Data, Size);
	FHeader& H = Out.Header;
	if (!R.Read(H.Magic) || !R.Read(H.Version) || !R.Read(H.Type) || !R.Read(H.Seq) || !R.Read(H.SenderId) ||
		!R.Read(H.PayloadBytes))
		return Fail("short header");
	if (H.Magic != kMagic) return Fail("bad magic");
	if (H.Version != kVersion) return Fail("unsupported version");
	if (Size != kHeaderBytes + H.PayloadBytes) return Fail("payload length mismatch");

	switch (static_cast<EType>(H.Type))
	{
	case EType::Frame:
	{
		uint16_t N = 0, Reserved = 0;
		if (!R.Read(Out.SimTime) || !R.Read(Out.SendTime) || !R.Read(N) || !R.Read(Reserved)) return Fail("truncated frame");
		Out.Entities.clear();
		Out.Entities.reserve(N);
		for (uint16_t i = 0; i < N; ++i)
		{
			FEntity E;
			uint16_t NC = 0;
			bool Ok = R.Read(E.Id);
			for (double& V : E.Pos) Ok = Ok && R.Read(V);
			for (double& V : E.Vel) Ok = Ok && R.Read(V);
			for (double& V : E.Q) Ok = Ok && R.Read(V);
			Ok = Ok && R.Read(E.EngineMask) && R.Read(E.Flags) && R.Read(NC);
			if (!Ok) return Fail("truncated entity");
			E.Channels.reserve(NC);
			for (uint16_t c = 0; c < NC; ++c)
			{
				std::string Name;
				double Value = 0.0;
				if (!R.ReadString(Name) || !R.Read(Value)) return Fail("truncated channel");
				E.Channels.emplace_back(std::move(Name), Value);
			}
			Out.Entities.push_back(std::move(E));
		}
		break;
	}
	case EType::Event:
		if (!R.Read(Out.SimTime) || !R.Read(Out.EventId) || !R.ReadString(Out.EventName)) return Fail("truncated event");
		break;
	case EType::Scene:
		R.ReadRest(Out.SceneJson);
		break;
	default:
		return Fail("unknown type");
	}
	if (!R.AtEnd()) return Fail("trailing bytes");
	return true;
}

// Keeps the first copy of each event: senders repeat events because UDP may drop them.
class FEventDedup
{
public:
	// true if (SenderId, EventId) is new
	bool Accept(uint32_t SenderId, uint32_t EventId)
	{
		if (SenderId != Sender)
		{
			Sender = SenderId;
			Seen.clear();
		}
		for (uint32_t Id : Seen)
			if (Id == EventId) return false;
		Seen.push_back(EventId);
		if (Seen.size() > 256) Seen.erase(Seen.begin());
		return true;
	}

private:
	uint32_t Sender = 0;
	std::vector<uint32_t> Seen;
};
}  // namespace UrvWire
