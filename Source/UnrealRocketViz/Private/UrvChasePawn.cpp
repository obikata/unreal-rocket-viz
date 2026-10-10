#include "UrvChasePawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "UrvDirector.h"
#include "UrvSoundscape.h"
#include "UrvHud.h"
#include "UrvPaths.h"
#include "UrvVehicle.h"

namespace
{
	constexpr double CamCmPerM = 100.0;
	constexpr double Aspect = 16.0 / 9.0;

	// Smooth noise in -1..1 for camera shake; Seed separates the axes.
	double Wobble(double T, double Hz, double Seed)
	{
		return FMath::PerlinNoise1D(float(T * Hz + Seed * 17.31)) * 1.4;
	}

	FRotator Shake(double T, double Deg, double Hz)
	{
		return FRotator(Wobble(T, Hz, 1) * Deg, Wobble(T, Hz, 2) * Deg, Wobble(T, Hz, 3) * Deg * 0.5);
	}
}

AUrvChasePawn::AUrvChasePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostPhysics;   // after the director has moved the vehicles
	AutoPossessPlayer = EAutoReceiveInput::Player0;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("Arm"));
	Arm->SetupAttachment(RootComponent);
	Arm->SetUsingAbsoluteRotation(true);
	Arm->bDoCollisionTest = false;
	// No positional lag: at km/s any lag turns frame-time jitter into the vehicle sliding back and forth.
	Arm->bEnableCameraLag = false;
	// The arm must place the camera after this pawn has followed the vehicle in the same frame.
	Arm->PrimaryComponentTick.TickGroup = TG_PostPhysics;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Arm);
	Camera->SetFieldOfView(ChaseFovDeg);
}

void AUrvChasePawn::BeginPlay()
{
	Super::BeginPlay();
	Arm->AddTickPrerequisiteActor(this);
}

void AUrvChasePawn::SetView(EUrvView InView, int32 InTarget)
{
	View = InView;
	if (Targets.IsValidIndex(InTarget))
	{
		TargetIndex = InTarget;
	}
}

const AUrvVehicle* AUrvChasePawn::GetOnboardVehicle() const
{
	return View == EUrvView::Onboard && Mounts.IsValidIndex(TargetIndex) && Targets.IsValidIndex(TargetIndex)
		? Targets[TargetIndex].Get() : nullptr;
}

void AUrvChasePawn::HandleInput()
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !bAcceptInput)
	{
		return;
	}
	PC->bShowMouseCursor = true;
	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
		EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 i = 0; i < Targets.Num() && i < 9; ++i)
	{
		if (PC->WasInputKeyJustPressed(Digits[i]))
		{
			TargetIndex = i;
			bAutoCuts = false;
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::V))
	{
		View = static_cast<EUrvView>((static_cast<uint8>(View) + 1) % 3);
		bAutoCuts = false;
	}
	if (Paths && PC->WasInputKeyJustPressed(EKeys::P))
	{
		Paths->CyclePathMode();
	}
	if (Paths && PC->WasInputKeyJustPressed(EKeys::T))
	{
		Paths->ToggleTrail();
	}
	if (PC->WasInputKeyJustPressed(EKeys::H))
	{
		if (AUrvHud* Hud = Cast<AUrvHud>(PC->GetHUD()))
		{
			Hud->ToggleOverlay();
		}
	}
	if (PC->WasInputKeyJustPressed(EKeys::A))
	{
		bAutoCuts = true;
		NextCut = 0;   // ApplyCuts skips ahead to the cut in force now
	}
	if (View != EUrvView::Chase)
	{
		return;
	}
	if (PC->IsInputKeyDown(EKeys::LeftMouseButton))
	{
		float Dx = 0.0f, Dy = 0.0f;
		PC->GetInputMouseDelta(Dx, Dy);
		Yaw += Dx * 2.0;
		Pitch = FMath::Clamp(Pitch + Dy * 2.0, -89.0, 89.0);
	}
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) ArmM = FMath::Max(20.0, ArmM * 0.85);
	if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) ArmM = FMath::Min(200000.0, ArmM * 1.18);
}

void AUrvChasePawn::ApplyCuts()
{
	const FUrvFrame* F = Director ? Director->GetDisplayFrame() : nullptr;
	if (!bAutoCuts || !F)
	{
		return;
	}
	int32 Due = -1;
	for (int32 i = NextCut; i < Cuts.Num() && Cuts[i].SimTime <= F->SimTime; ++i)
	{
		Due = i;
	}
	if (Due >= 0)
	{
		SetView(Cuts[Due].View, Cuts[Due].Target);
		NextCut = Due + 1;
	}
}

FVector AUrvChasePawn::AimPoint(const AUrvVehicle* T) const
{
	const double Aim = AimX.IsValidIndex(TargetIndex) ? AimX[TargetIndex] : 0.0;
	return T->GetActorLocation() + T->GetActorForwardVector() * Aim * CamCmPerM;
}

void AUrvChasePawn::TickChase(const AUrvVehicle* T, FVector& Loc, FRotator& Rot)
{
	// Orbit in the frame of the georeference origin (world Z is local up there).
	Rot = FRotator(Pitch, Yaw, 0.0);
	Loc = AimPoint(T) - Rot.Vector() * ArmM * CamCmPerM;
	Camera->SetFieldOfView(ChaseFovDeg);
}

void AUrvChasePawn::TickDrone(const AUrvVehicle* T, float Dt, FVector& Loc, FRotator& Rot)
{
	const FUrvDroneCamera& D = Drone;
	if (!bDroneReady)
	{
		DroneHome = T->GetActorLocation() + D.HomeOffsetM * CamCmPerM;
		DronePos = DroneHome;
		Gimbal = (AimPoint(T) - DronePos).Rotation().Quaternion();
		bDroneReady = true;
	}
	// Hover height above the ground under the drone, once the terrain there is known.
	if (!bDroneGroundAsked && Director)
	{
		bDroneGroundAsked = true;
		TWeakObjectPtr<AUrvChasePawn> Self(this);
		Director->SampleGround(DroneHome, [Self](bool bOk, const FVector& Ground)
		{
			if (!Self.IsValid())
			{
				return;
			}
			if (!bOk)
			{
				Self->bDroneGroundAsked = false;   // ask again
				return;
			}
			const double Z = Ground.Z + Self->Drone.HomeOffsetM.Z * CamCmPerM;
			Self->DronePos.Z += Z - Self->DroneHome.Z;
			Self->DroneHome.Z = Z;
		});
	}
	// Climb once the target is under way.
	if (T->GetVelocity().Size() > 2.0 * CamCmPerM && DronePos.Z < DroneHome.Z + D.MaxClimbM * CamCmPerM)
	{
		DronePos.Z = FMath::Min(DronePos.Z + D.ClimbRateMS * CamCmPerM * Dt, DroneHome.Z + D.MaxClimbM * CamCmPerM);
	}
	// Climb while the ground hides the target, as an operator would.
	FHitResult Block;
	if (GetWorld()->LineTraceSingleByChannel(Block, DronePos, AimPoint(T), ECC_Visibility))
	{
		const double Up = 8.0 * CamCmPerM * Dt;
		DroneHome.Z += Up;
		DronePos.Z += Up;
	}
	// Hover drift of a few tens of centimetres.
	const FVector Drift(Wobble(Clock, 0.07, 4), Wobble(Clock, 0.06, 5), Wobble(Clock, 0.09, 6) * 0.5);
	Loc = DronePos + Drift * 30.0;

	// Gimbal: lags the target by GimbalTimeS; leading by the same time keeps it centred on average.
	const FVector Aim = AimPoint(T) + T->GetVelocity() * D.GimbalTimeS;
	FRotator Want = (Aim - Loc).Rotation();
	Want.Pitch = FMath::Min(Want.Pitch, D.MaxGimbalPitchDeg);
	Want.Roll = 0.0;
	Gimbal = FQuat::Slerp(Gimbal, Want.Quaternion(), 1.0 - FMath::Exp(-Dt / D.GimbalTimeS));
	Rot = Gimbal.Rotator();

	// Zoom so that FrameSizeM spans FrameFraction of the frame height.
	const double Dist = FMath::Max((AimPoint(T) - Loc).Size() / CamCmPerM, 1.0);
	const double VFov = 2.0 * FMath::Atan(D.FrameSizeM / 2.0 / Dist) / D.FrameFraction;
	const double HFov = FMath::RadiansToDegrees(2.0 * FMath::Atan(FMath::Tan(VFov / 2.0) * Aspect));
	const double WantFov = FMath::Clamp(HFov, D.MinFovDeg, D.MaxFovDeg);
	DroneFovDeg = FMath::Exp(FMath::Lerp(FMath::Loge(DroneFovDeg), FMath::Loge(WantFov), 1.0 - FMath::Exp(-Dt / D.ZoomTimeS)));
	Camera->SetFieldOfView(float(DroneFovDeg));
}

bool AUrvChasePawn::TickOnboard(const AUrvVehicle* T, FVector& Loc, FRotator& Rot, double& Vibration)
{
	if (!Mounts.IsValidIndex(TargetIndex))
	{
		return false;
	}
	const FUrvMountedCamera& M = Mounts[TargetIndex];
	const FTransform X = T->GetActorTransform();
	Loc = X.TransformPosition(M.LocationM * CamCmPerM);
	Rot = (X.GetRotation() * M.Rotation.Quaternion()).Rotator();
	Camera->SetFieldOfView(M.FovDeg);
	Vibration = T->IsEngineOn() ? M.VibrationDeg + M.VibrationPerKPaDeg * T->GetDynamicPressurePa() / 1000.0 : 0.0;
	return true;
}

void AUrvChasePawn::SetPanini(float D)
{
	if (D == PaniniNow)
	{
		return;
	}
	PaniniNow = D;
	if (IConsoleVariable* V = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Upscale.Panini.D")))
	{
		V->Set(D, ECVF_SetByCode);
	}
}

void AUrvChasePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	HandleInput();
	ApplyCuts();

	if (!Targets.IsValidIndex(TargetIndex) || !Targets[TargetIndex])
	{
		return;
	}
	const AUrvVehicle* T = Targets[TargetIndex];
	FVector Loc;
	FRotator Rot;
	double Vibration = 0.0;
	float Panini = 0.0f;
	const double Loud = Soundscape ? Soundscape->GetShake() : 0.0;
	switch (View)
	{
	case EUrvView::Drone:
		TickDrone(T, DeltaSeconds, Loc, Rot);
		Rot += Shake(Clock, 0.012 * Loud, 9.0);   // the gimbal absorbs most of it
		break;
	case EUrvView::Onboard:
		if (TickOnboard(T, Loc, Rot, Vibration))
		{
			Panini = Mounts[TargetIndex].Panini;
			Rot += Shake(Clock, Vibration, 23.0) + Shake(Clock, Vibration * 0.5, 4.0);
			break;
		}
		[[fallthrough]];
	default:
		TickChase(T, Loc, Rot);
		Rot += Shake(Clock, 0.08 * Loud, 7.0);
		break;
	}
	SetPanini(Panini);
	SetActorLocation(Loc);
	Arm->TargetArmLength = 0.0f;
	Arm->SetWorldRotation(Rot);
}
