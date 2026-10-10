#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UrvTypes.h"
#include "UrvDirector.generated.h"

class ACesiumGeoreference;
class AUrvVehicle;

// Plays time-stamped states onto vehicle actors. Feed it from any thread
// (a network receiver, a log reader, ...): PushFrame / PushEvent / SetLinkStatus.
//
// The display clock follows SimTime and runs Delay behind the newest frame,
// interpolating between the two frames around it (position linear, attitude slerp).
UCLASS()
class UNREALROCKETVIZ_API AUrvDirector : public AActor
{
	GENERATED_BODY()

public:
	AUrvDirector();

	UPROPERTY(EditAnywhere, Category = "UnrealRocketViz")
	double Delay = 0.1;

	UPROPERTY()
	TObjectPtr<ACesiumGeoreference> Georeference;

	// Bind an entity id to the actor that shows it.
	void Bind(int32 EntityId, AUrvVehicle* Vehicle, const FString& Label);

	void PushFrame(const FUrvFrame& Frame);
	void PushEvent(double SimTime, const FString& Name);
	void SetLinkStatus(const FString& Text);

	// Optional: shift every vehicle vertically so that this point (longitude,
	// latitude in degrees, height in metres above WGS84) ends up AboveGroundM
	// above the streamed terrain, e.g. a launch pad deck or a landing site.
	// The terrain height comes from its most detailed tiles. Without it, positions are drawn exactly as sent.
	void SetGroundReference(const FVector& LonLatHeight, double AboveGroundM = 0.0);
	// The same, with the point taken from where entity EntityId is in the first frame that
	// carries it: that entity then stands AboveGroundM above the terrain, whatever height
	// the sender put its pad at.
	void SetGroundReferenceFromEntity(int32 EntityId, double AboveGroundM = 0.0);
	// The reference point in use (degrees, degrees, metres); known once HasGround().
	FVector GetGroundReferenceLonLatHeight() const { return GroundRefLlh; }
	// The shift SetGroundReference applies to vehicles [Unreal cm], for scenery that must move with them.
	FVector GetGroundOffset() const { return GroundOffset; }
	bool HasGround() const { return bGroundFound; }

	// The streamed terrain's surface under (or over) WorldPos, from its most detailed tiles,
	// independent of what is loaded or drawn. Asynchronous; OnGround runs on the game thread
	// with false if there is no terrain there.
	void SampleGround(const FVector& WorldPos, TFunction<void(bool bOk, const FVector& GroundWorld)> OnGround);

	virtual void Tick(float DeltaSeconds) override;

	// What is on screen this frame (game thread). Null until the first frame arrives.
	const FUrvFrame* GetDisplayFrame() const { return bHaveDisplay ? &DisplayFrame : nullptr; }
	const FUrvEvent* GetLastEvent() const { return bHaveEvent ? &LastEvent : nullptr; }
	FString GetLinkStatus() const;
	double GetLatencyMs() const { return LatencyMs; }

	// Old-style text readout through on-screen debug messages.
	UPROPERTY(EditAnywhere, Category = "UnrealRocketViz")
	bool bDebugText = false;

private:
	struct FBinding
	{
		int32 Id = 0;
		TWeakObjectPtr<AUrvVehicle> Vehicle;
		FString Label;
	};

	bool Sample(double T, FUrvFrame& Out) const;
	void Place(AUrvVehicle* V, const FUrvEntityState& E) const;
	void UpdateGroundOffset(double Now);
	void DrawHud(const FUrvFrame& F) const;

	TArray<FBinding> Bindings;

	// Shared with the feeding thread.
	mutable FCriticalSection Lock;
	TArray<FUrvFrame> Buffer;
	double LastPushAt = 0.0;   // FPlatformTime::Seconds() of the newest PushFrame
	TArray<FUrvEvent> PendingEvents;
	FString LinkStatus;

	// Game thread only.
	FUrvFrame DisplayFrame;
	bool bHaveDisplay = false;
	double Clock = 0.0;
	bool bClockRunning = false;
	double LatencyMs = 0.0;
	FUrvEvent LastEvent;
	bool bHaveEvent = false;
	bool bHasGroundRef = false;
	FVector GroundRefLlh = FVector::ZeroVector;
	double GroundRefAboveM = 0.0;
	FVector GroundOffset = FVector::ZeroVector;   // Unreal cm, added to every vehicle position
	double GroundLastTry = 0.0;
	bool bGroundAsked = false;
	int32 GroundRefEntity = -1;   // >= 0: the reference point is still to be taken from this entity
	bool bGroundFound = false;
};
