#include "UrvUdpReceiver.h"

#include "Async/Async.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
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
	FParse::Value(FCommandLine::Get(), TEXT("UrvGroup="), Group);
	FParse::Value(FCommandLine::Get(), TEXT("UrvPort="), Port);

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
	UrvWire::FMessage M;
	std::string Err;
	if (!Data.IsValid() || !UrvWire::Decode(Data->GetData(), Data->Num(), M, &Err))
	{
		++Errors;
		return;
	}
	++Packets;
	if (M.Header.SenderId != LastSender)
	{
		LastSender = M.Header.SenderId;
		LastSceneJson.Reset();   // a restarted sender: take its scene again
	}

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
			E.bEngineOn = (W.Flags & 1u) != 0;
			E.EngineMask = W.EngineMask;
			for (const auto& C : W.Channels)
			{
				E.Channels.Add(FName(UTF8_TO_TCHAR(C.first.c_str())), C.second);
			}
		}
		if (Director)
		{
			Director->PushFrame(F);
		}
		break;
	}
	case UrvWire::EType::Event:
		if (Director && Dedup.Accept(M.Header.SenderId, M.EventId))
		{
			Director->PushEvent(M.SimTime, UTF8_TO_TCHAR(M.EventName.c_str()));
		}
		break;
	case UrvWire::EType::Scene:
	{
		FString Json = UTF8_TO_TCHAR(M.SceneJson.c_str());
		if (Json != LastSceneJson)
		{
			LastSceneJson = Json;
			TWeakObjectPtr<AUrvUdpReceiver> Self(this);
			AsyncTask(ENamedThreads::GameThread, [Self, Json]() {
				if (AUrvUdpReceiver* R = Self.Get())
				{
					R->OnScene.Broadcast(Json);
				}
			});
		}
		break;
	}
	}
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
	Director->SetLinkStatus(N == 0 ? FString::Printf(TEXT("UDP %d  WAITING"), Port)
		: FString::Printf(TEXT("UDP %d  %.0f PKT/S%s"), Port, Rate,
			E > 0 ? *FString::Printf(TEXT("  %lld BAD"), E) : TEXT("")));
}
