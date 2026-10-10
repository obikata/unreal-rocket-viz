#include "UrvUdpReceiver.h"

#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Serialization/ArrayReader.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "UrvDirector.h"

AUrvUdpReceiver::AUrvUdpReceiver()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.5f;   // link status only
}

void AUrvUdpReceiver::BeginPlay()
{
	Super::BeginPlay();
	if (Director && !Socket)
	{
		Start();
	}
}

void AUrvUdpReceiver::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Stop();
	Super::EndPlay(EndPlayReason);
}

bool AUrvUdpReceiver::Start()
{
	Stop();
	const FString PortEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("URV_UDP_PORT"));
	if (!PortEnv.IsEmpty())
	{
		Port = FCString::Atoi(*PortEnv);
	}
	FParse::Value(FCommandLine::Get(), TEXT("UrvGroup="), Group);
	FParse::Value(FCommandLine::Get(), TEXT("UrvPort="), Port);
	bAnySeq = false;
	LastSender = 0;
	LastSceneJson.Reset();

	FUdpSocketBuilder Builder(TEXT("UrvUdpReceiver"));
	Builder.AsNonBlocking()
		.AsReusable()
		.BoundToAddress(FIPv4Address::Any)
		.BoundToPort(Port)
		.WithReceiveBufferSize(4 * 1024 * 1024);
	const bool bMulticast = !Group.IsEmpty() && !Group.Equals(TEXT("none"), ESearchCase::IgnoreCase);
	if (bMulticast)
	{
		FIPv4Address GroupAddress;
		if (!FIPv4Address::Parse(Group, GroupAddress))
		{
			UE_LOG(LogTemp, Error, TEXT("UnrealRocketViz: bad multicast group '%s'"), *Group);
			return false;
		}
		Builder.JoinedToGroup(GroupAddress).WithMulticastLoopback();
	}
	Socket = Builder.Build();
	if (!Socket)
	{
		UE_LOG(LogTemp, Error, TEXT("UnrealRocketViz: cannot open UDP port %d"), Port);
		if (Director)
		{
			Director->SetLinkStatus(FString::Printf(TEXT("UDP %d: CANNOT OPEN"), Port));
		}
		return false;
	}
	Receiver = new FUdpSocketReceiver(Socket, FTimespan::FromMilliseconds(100), TEXT("UrvUdpReceiver"));
	Receiver->OnDataReceived().BindUObject(this, &AUrvUdpReceiver::OnDatagram);
	Receiver->Start();
	UE_LOG(LogTemp, Log, TEXT("UnrealRocketViz: listening on UDP %d%s%s"), Port, bMulticast ? TEXT(", group ") : TEXT(""),
		bMulticast ? *Group : TEXT(""));
	if (Director)
	{
		Director->SetLinkStatus(FString::Printf(TEXT("UDP %s:%d  WAITING"), bMulticast ? *Group : TEXT("*"), Port));
	}
	StatusAt = FPlatformTime::Seconds();
	return true;
}

void AUrvUdpReceiver::Stop()
{
	if (Receiver)
	{
		Receiver->Stop();   // joins the thread: no callback runs after this
		delete Receiver;
		Receiver = nullptr;
	}
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
}

void AUrvUdpReceiver::OnDatagram(const TSharedPtr<FArrayReader, ESPMode::ThreadSafe>& Data, const FIPv4Endpoint& From)
{
	if (Data.IsValid())
	{
		HandleDatagram(Data->GetData(), Data->Num());
	}
}

void AUrvUdpReceiver::HandleDatagram(const uint8* Data, int32 Size)
{
	UrvWire::FMessage M;
	std::string Err;
	if (!UrvWire::Decode(Data, std::size_t(Size), M, &Err))
	{
		NoteBad();
		return;
	}
	NoteSequence(M.Header.SenderId, M.Header.Seq);

	switch (static_cast<UrvWire::EType>(M.Header.Type))
	{
	case UrvWire::EType::Frame:
	{
		FUrvFrame F;
		F.SimTime = M.SimTime;
		F.SendTime = M.SendTime;
		F.Entities.Reserve(M.Entities.size());
		for (const UrvWire::FEntity& W : M.Entities)
		{
			FUrvEntityState& E = F.Entities.AddDefaulted_GetRef();
			E.Id = W.Id;
			E.PosEcef = FVector(W.Pos[0], W.Pos[1], W.Pos[2]);
			E.VelEcef = FVector(W.Vel[0], W.Vel[1], W.Vel[2]);
			E.QBody2Ecef = FQuat(W.Q[1], W.Q[2], W.Q[3], W.Q[0]).GetNormalized();   // FQuat is (X, Y, Z, W)
			E.OmegaBody = FVector(W.Omega[0], W.Omega[1], W.Omega[2]);
			E.Gimbal = FVector2D(W.Gimbal[0], W.Gimbal[1]);
			E.Throttle = W.Throttle;
			E.bEngineOn = (W.Flags & 1u) != 0;
			E.EngineMask = W.EngineMask;
			for (const auto& C : W.Channels)
			{
				E.Channels.Add(FName(UTF8_TO_TCHAR(C.first.c_str())), C.second);
			}
		}
		EmitFrame(F);
		break;
	}
	case UrvWire::EType::Event:
		EmitEvent(M.Header.SenderId, M.EventId, M.SimTime, UTF8_TO_TCHAR(M.EventName.c_str()));
		break;
	case UrvWire::EType::Scene:
		EmitScene(UTF8_TO_TCHAR(M.SceneJson.c_str()));
		break;
	}
}

void AUrvUdpReceiver::NoteSequence(uint32 SenderId, uint32 Seq)
{
	++Packets;
	if (!bAnySeq || SenderId != LastSender)
	{
		LastSender = SenderId;      // a (re)started sender
		LastSceneJson.Reset();      // take its scene again
		bAnySeq = true;
		NextSeq = Seq + 1;
		return;
	}
	const uint32 Gap = Seq - NextSeq;   // modulo 2^32
	if (Gap < 0x80000000u)              // ahead: anything skipped is lost
	{
		Lost += Gap;
		NextSeq = Seq + 1;
	}
	// behind: a late or duplicated datagram, already counted
}

void AUrvUdpReceiver::EmitFrame(const FUrvFrame& Frame)
{
	if (Director)
	{
		Director->PushFrame(Frame);
	}
}

void AUrvUdpReceiver::EmitEvent(uint32 SenderId, uint32 EventId, double SimTime, const FString& Name)
{
	if (Director && Dedup.Accept(SenderId, EventId))
	{
		Director->PushEvent(SimTime, Name);
	}
}

void AUrvUdpReceiver::EmitScene(const FString& Json)
{
	if (Json == LastSceneJson)
	{
		return;
	}
	LastSceneJson = Json;
	TWeakObjectPtr<AUrvUdpReceiver> Self(this);
	AsyncTask(ENamedThreads::GameThread, [Self, Json]() {
		if (AUrvUdpReceiver* R = Self.Get())
		{
			R->OnScene.Broadcast(Json);
		}
	});
}

void AUrvUdpReceiver::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Director || !Socket)
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const int64 N = Packets.load();
	const double Rate = (N - StatusPackets) / FMath::Max(Now - StatusAt, 1e-3);
	StatusAt = Now;
	StatusPackets = N;
	const int64 E = Errors.load();
	const int64 L = Lost.load();
	FString Status = N == 0 ? FString::Printf(TEXT("UDP %d  WAITING"), Port) : FString::Printf(TEXT("UDP %d  %.0f HZ"), Port, Rate);
	if (L > 0)
	{
		Status += FString::Printf(TEXT("  LOST %lld"), L);
	}
	if (E > 0)
	{
		Status += FString::Printf(TEXT("  BAD %lld"), E);
	}
	Director->SetLinkStatus(Status);
}
