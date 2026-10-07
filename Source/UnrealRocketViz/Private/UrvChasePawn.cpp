#include "UrvChasePawn.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "UrvVehicle.h"

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
	Camera->SetFieldOfView(50.0f);
}

void AUrvChasePawn::BeginPlay()
{
	Super::BeginPlay();
	Arm->AddTickPrerequisiteActor(this);
}

void AUrvChasePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = true;
		static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
			EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
		for (int32 i = 0; i < Targets.Num() && i < 9; ++i)
		{
			if (PC->WasInputKeyJustPressed(Digits[i]))
			{
				TargetIndex = i;
			}
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

	if (!Targets.IsValidIndex(TargetIndex) || !Targets[TargetIndex])
	{
		return;
	}
	const AUrvVehicle* T = Targets[TargetIndex];
	const double Aim = AimX.IsValidIndex(TargetIndex) ? AimX[TargetIndex] : 0.0;
	SetActorLocation(T->GetActorLocation() + T->GetActorForwardVector() * Aim * 100.0);
	// Orbit in the frame of the georeference origin (world Z is local up there).
	Arm->SetWorldRotation(FRotator(Pitch, Yaw, 0.0));
	Arm->TargetArmLength = ArmM * 100.0;
}
