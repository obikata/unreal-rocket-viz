#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UrvWire.h"
#include "UrvUdpReceiver.generated.h"

class AUrvDirector;
class FSocket;
class FUdpSocketReceiver;
struct FIPv4Endpoint;

// A new or changed SCENE (Docs/wire-protocol.md), as its JSON text. Game thread.
DECLARE_MULTICAST_DELEGATE_OneParam(FUrvSceneReceived, const FString& /*Json*/);

// Subscribes to URV wire protocol datagrams (Docs/wire-protocol.md) and feeds
// them to a director: FRAMEs become FUrvFrame, EVENTs are de-duplicated and
// pushed by name, SCENEs are handed to OnScene on the game thread whenever
// their content changes. Receives on its own thread; the director is
// thread-safe by design.
//
// Overrides from the command line: -UrvGroup=239.255.76.86 -UrvPort=47686
// (-UrvGroup=none listens for unicast only).
UCLASS()
class UNREALROCKETVIZ_API AUrvUdpReceiver : public AActor
{
	GENERATED_BODY()

public:
	AUrvUdpReceiver();

	// Multicast group to join; empty or "none" receives unicast datagrams only.
	UPROPERTY(EditAnywhere, Category = "UnrealRocketViz")
	FString Group = TEXT("239.255.76.86");   // UrvWire::kDefaultGroup

	UPROPERTY(EditAnywhere, Category = "UnrealRocketViz")
	int32 Port = 47686;                      // UrvWire::kDefaultPort

	UPROPERTY()
	TObjectPtr<AUrvDirector> Director;

	FUrvSceneReceived OnScene;

	// Opens the socket and starts the receive thread (also called from BeginPlay if Director is set).
	bool Start();
	void Stop();

	// Datagrams accepted / rejected since Start.
	int64 GetPacketCount() const { return Packets; }
	int64 GetErrorCount() const { return Errors; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void OnDatagram(const TSharedPtr<class FArrayReader, ESPMode::ThreadSafe>& Data, const FIPv4Endpoint& From);

	FSocket* Socket = nullptr;
	FUdpSocketReceiver* Receiver = nullptr;

	// Receive thread only.
	UrvWire::FEventDedup Dedup;
	uint32 LastSender = 0;
	FString LastSceneJson;

	std::atomic<int64> Packets{0};
	std::atomic<int64> Errors{0};
	double StatusAt = 0.0;
	int64 StatusPackets = 0;
};
