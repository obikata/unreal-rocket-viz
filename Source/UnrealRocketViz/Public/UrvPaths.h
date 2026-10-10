#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UrvPaths.generated.h"

class AUrvDirector;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
struct FUrvFrame;

// How one kind of path is drawn (SCENE "paths", matched by name).
struct FUrvPathStyle
{
	FString Name;                                       // the PATH name, e.g. "plan"
	FString Label;                                      // e.g. "GUIDANCE PLAN"
	FLinearColor Color = FLinearColor(0.22f, 0.74f, 0.97f);
	bool bDashed = false;
	int32 Ghosts = 6;                                   // earlier versions kept, fading
};

UENUM()
enum class EUrvPathMode : uint8
{
	Current,      // the newest version of each path
	WithGhosts,   // ... and the earlier ones, fading: how a plan was revised
	Hidden
};

// Draws the sender's polylines (PATH messages, e.g. the current guidance plan
// and the plans it replaced) and each entity's flown trail, synchronised with
// the director's display clock, as glowing camera-facing ribbons (M_UrvPath) of
// constant on-screen width; the newest plan pulses toward its end, which gets a ring.
//
// PushPath is called from the receive thread; everything else on the game thread.
UCLASS()
class UNREALROCKETVIZ_API AUrvPaths : public AActor
{
	GENERATED_BODY()

public:
	AUrvPaths();

	UPROPERTY()
	TObjectPtr<AUrvDirector> Director;

	TArray<FUrvPathStyle> Styles;
	// Trail colour: this channel mapped green -> amber -> red over [0, TrailMax]; None: white.
	FName TrailChannel;
	double TrailMax = 1.0;

	EUrvPathMode PathMode = EUrvPathMode::WithGhosts;
	bool bShowTrail = true;
	// Emissive brightness relative to a daylit scene (Cesium's physical sun exposes for
	// ~2000 nits, like the plume): 1 is about as bright as a sunlit white surface.
	float Glow = 3.0f;
	double LinePixels = 26.0;       // on-screen width of the newest path's ribbon, glow included [px]
	float Pastel = 0.35f;           // colours lightened toward white by this much (0: as given)
	float Opacity = 0.7f;           // of the newest path; ghosts and the trail scale from it
	double TrailSpacing = 0.1;      // [s] of sim time between trail points
	int32 TrailMaxPoints = 6000;

	// Any thread. Keeps the highest Version per (entity, name); a new sender starts over.
	void PushPath(uint32 SenderId, int32 EntityId, const FString& Name, uint32 Version, double SimTime,
		TArray<FVector> PointsEcef);

	void CyclePathMode();
	void ToggleTrail();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FPath
	{
		int32 Entity = 0;
		FString Name;
		uint32 Version = 0;
		double SimTime = 0.0;
		TArray<FVector> Ecef;
	};
	struct FTrailPoint
	{
		FVector Ecef;
		double Value = 0.0;
		bool bHasValue = false;
	};

	// Shared with the receive thread.
	FCriticalSection Lock;
	TArray<FPath> Pending;               // received, waiting for the display clock
	TMap<FString, uint32> Latest;        // "entity/name" -> highest version accepted
	uint32 Sender = 0;
	bool bSenderChanged = false;

	// Game thread.
	TArray<FPath> Shown;                 // per (entity, name): oldest first, newest last
	TMap<int32, TArray<FTrailPoint>> Trails;
	double LastTrailT = -1e300;
	double LastDisplayT = -1e300;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Mesh;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> RibbonMaterial;

	const FUrvPathStyle& StyleFor(const FString& Name) const;
	void ClearShown();
	void Draw(const FUrvFrame& F);
};
