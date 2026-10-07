#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "UrvChasePawn.generated.h"

class UCameraComponent;
class USpringArmComponent;
class AUrvVehicle;

// Follows one vehicle. Drag with the left mouse button to orbit, wheel to zoom,
// number keys 1..9 pick the target.
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

	int32 GetTargetIndex() const { return TargetIndex; }

private:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USpringArmComponent> Arm;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	int32 TargetIndex = 0;
	double Yaw = 150.0;
	double Pitch = -8.0;
	double ArmM = 110.0;
};
