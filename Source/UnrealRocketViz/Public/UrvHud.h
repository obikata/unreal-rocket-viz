#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UrvTypes.h"
#include "UrvHud.generated.h"

class AUrvDirector;

// The entity whose readouts are shown while the chase camera follows target i.
struct FUrvGaugeGroup
{
	int32 EntityId = 0;
	FString Label;        // e.g. TEXT("第1段")
	FString LabelEn;      // e.g. "STAGE 1"
};

// One value beside the clock, read from a telemetry channel exactly as sent.
struct FUrvReadout
{
	FName Channel;       // e.g. "speed_kmh"
	FString Label;       // e.g. "SPEED"
	FString Unit;        // e.g. "KM/H"
	int32 Decimals = 0;
	double Max = 0.0;    // value at a full gauge arc; 0 draws no arc
};

// A planned milestone, used to give events their display names.
struct FUrvMilestone
{
	double Time = 0.0;   // planned sim time [s]
	FString Name;        // e.g. TEXT("段分離")
	FString Code;        // event name as the sender sends it, e.g. "STAGE_SEP"
	FString NameEn;      // e.g. "STAGE SEPARATION"
};

// Minimal overlay along the bottom edge: the mission clock with readouts either
// side of it, and the latest event's name. Readouts come from telemetry
// channels exactly as the sender reports them; nothing is computed here.
UCLASS()
class UNREALROCKETVIZ_API AUrvHud : public AHUD
{
	GENERATED_BODY()

public:
	TObjectPtr<AUrvDirector> Director;
	FString MissionName;            // e.g. TEXT("試験飛行")
	FString Credit;                 // small print at the lower right, e.g. imagery attribution
	TArray<FUrvGaugeGroup> Groups;  // index = chase camera target
	TArray<FUrvMilestone> Milestones;
	// Placed alternately left and right of the clock, inside out.
	TArray<FUrvReadout> Readouts = {
		{ TEXT("speed_kmh"), TEXT("SPEED"), TEXT("KM/H"), 0, 30000.0 },
		{ TEXT("altitude_km"), TEXT("ALTITUDE"), TEXT("KM"), 1, 300.0 } };

	// Index of the group the chase camera follows.
	int32 GetFollowedIndex() const;
	const FUrvMilestone* FindMilestone(const FString& Code) const
	{
		return Milestones.FindByPredicate([&Code](const FUrvMilestone& M) { return M.Code == Code; });
	}

protected:
	// Drawn by a Slate overlay (anti-aliased, native resolution), not the Canvas.
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	TSharedPtr<class SWidget> Overlay;
};
