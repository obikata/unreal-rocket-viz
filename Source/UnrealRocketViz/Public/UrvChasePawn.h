#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "UrvChasePawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class AUrvDirector;
class AUrvSoundscape;
class AUrvVehicle;

enum class EUrvView : uint8
{
	Chase,     // orbits the target; drag to turn, wheel to zoom
	Drone,     // hovers near the ground, gimbal and zoom track the target
	Onboard,   // fixed to the target's skin
};

// A hovering camera drone with a stabilised gimbal and a zoom lens, flown the
// way an operator would: it climbs once the target moves or while the ground hides
// it, the gimbal follows
// with a short lag, and the zoom keeps the target at a set size in the frame.
struct FUrvDroneCamera
{
	// Hover point [m, Unreal axes]: horizontally from the target's position when the
	// drone first flies, vertically above the ground found under it.
	FVector HomeOffsetM = FVector(-500.0, 0.0, 30.0);
	double ClimbRateMS = 6.0;
	double MaxClimbM = 140.0;             // above Home
	double FrameSizeM = 60.0;             // what is kept in frame, e.g. vehicle plus flame
	double FrameFraction = 0.55;          // of the frame height
	double MinFovDeg = 0.8;               // horizontal, longest zoom
	double MaxFovDeg = 82.0;              // horizontal, widest lens
	double ZoomTimeS = 1.2;
	double GimbalTimeS = 0.25;
	double MaxGimbalPitchDeg = 70.0;      // highest the gimbal tilts up
};

// A camera fixed to a vehicle, e.g. an action camera on the skin looking aft.
struct FUrvMountedCamera
{
	FVector LocationM = FVector::ZeroVector;    // in the vehicle's frame (+X nose)
	FRotator Rotation = FRotator::ZeroRotator;  // in the vehicle's frame; the camera looks along its +X
	float FovDeg = 120.0f;
	float Panini = 0.6f;                        // wide-angle projection (r.Upscale.Panini.D), 0 = rectilinear
	double VibrationDeg = 0.03;                 // with the engine on
	double VibrationPerKPaDeg = 0.004;          // added per kPa of dynamic pressure
};

// A cut in an automatic camera plan: from SimTime on, show View on target Target.
struct FUrvCut
{
	double SimTime = 0.0;
	EUrvView View = EUrvView::Chase;
	int32 Target = 0;
};

// The viewer's camera. Keys: 1..9 pick the target, V cycles the views, A resumes
// the automatic plan in Cuts (any manual choice pauses it).
UCLASS()
class UNREALROCKETVIZ_API AUrvChasePawn : public APawn
{
	GENERATED_BODY()

public:
	AUrvChasePawn();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

public:
	UPROPERTY()
	TArray<TObjectPtr<AUrvVehicle>> Targets;

	// Point the camera looks at, metres along each target's +X.
	TArray<double> AimX;

	FUrvDroneCamera Drone;
	TArray<FUrvMountedCamera> Mounts;   // index = target; a target without one has no onboard view
	TArray<FUrvCut> Cuts;               // in time order
	bool bAutoCuts = true;
	bool bAcceptInput = true;   // false: keys and mouse leave the camera alone (unattended capture)

	// Sim time for Cuts.
	UPROPERTY()
	TObjectPtr<AUrvDirector> Director;

	// Shakes the camera when the engine noise arrives.
	UPROPERTY()
	TObjectPtr<AUrvSoundscape> Soundscape;

	void SetView(EUrvView InView, int32 InTarget);
	// Chase camera orbit: pitch and yaw [deg] and distance [m].
	void SetOrbit(double InPitch, double InYaw, double InArmM) { Pitch = InPitch; Yaw = InYaw; ArmM = InArmM; }
	EUrvView GetView() const { return View; }
	int32 GetTargetIndex() const { return TargetIndex; }
	// The vehicle the camera rides on, or null.
	const AUrvVehicle* GetOnboardVehicle() const;

private:
	void HandleInput();
	void ApplyCuts();
	void TickChase(const AUrvVehicle* T, FVector& Loc, FRotator& Rot);
	void TickDrone(const AUrvVehicle* T, float Dt, FVector& Loc, FRotator& Rot);
	bool TickOnboard(const AUrvVehicle* T, FVector& Loc, FRotator& Rot, double& Vibration);
	FVector AimPoint(const AUrvVehicle* T) const;
	void SetPanini(float D);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> Arm;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	EUrvView View = EUrvView::Chase;
	int32 TargetIndex = 0;
	int32 NextCut = 0;
	double Yaw = 150.0;
	double Pitch = -8.0;
	double ArmM = 110.0;
	float ChaseFovDeg = 50.0f;
	float PaniniNow = -1.0f;

	// Drone state.
	FVector DroneHome = FVector::ZeroVector;
	FVector DronePos = FVector::ZeroVector;
	bool bDroneReady = false;
	bool bDroneGroundAsked = false;
	FQuat Gimbal = FQuat::Identity;
	double DroneFovDeg = 40.0;
	double Clock = 0.0;
};
