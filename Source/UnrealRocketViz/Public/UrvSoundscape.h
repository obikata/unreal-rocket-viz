#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UrvSoundscape.generated.h"

class UAudioComponent;
class UUrvRoarSynth;
class AUrvVehicle;
class AUrvChasePawn;

// Engine noise as heard at the camera. Each vehicle's noise leaves it at the
// speed of sound, so a distant camera hears ignition late, then quieter and
// duller with distance (air absorbs the highs). On board, the camera hears the
// structure: a low rumble that rises with dynamic pressure. The noise is
// synthesised, so no sound assets are needed.
UCLASS()
class UNREALROCKETVIZ_API AUrvSoundscape : public AActor
{
	GENERATED_BODY()

public:
	AUrvSoundscape();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY()
	TArray<TObjectPtr<AUrvVehicle>> Vehicles;

	// Where the camera is and whether it rides a vehicle.
	UPROPERTY()
	TObjectPtr<AUrvChasePawn> Listener;

	float Volume = 1.0f;
	double SpeedOfSoundMS = 340.0;
	double ReferenceM = 300.0;   // full level at or inside this distance

	// 0..1: how hard the arriving noise should shake an outside camera.
	double GetShake() const { return Shake; }

protected:
	virtual void BeginPlay() override;

private:
	struct FSample
	{
		double Time;
		FVector Pos;
		bool bOn;
	};

	UPROPERTY()
	TObjectPtr<UAudioComponent> Audio;

	UPROPERTY()
	TObjectPtr<UUrvRoarSynth> Synth;

	TArray<TArray<FSample>> History;   // per vehicle, oldest first
	double Clock = 0.0;
	double Shake = 0.0;
	double Kick = 0.0;                 // jolt when the noise front arrives
	bool bWasHearing = false;
};
