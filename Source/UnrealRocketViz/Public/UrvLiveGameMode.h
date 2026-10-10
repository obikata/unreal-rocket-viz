#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UrvScene.h"
#include "UrvVehicle.h"
#include "UrvLiveGameMode.generated.h"

class AUrvDirector;
class AUrvSoundscape;
class AUrvUdpReceiver;
class AUrvPaths;

// Everything needed to watch a live simulator: set this as a level's GameMode,
// press Play, start a sender (Docs/wire-protocol.md). The globe, vehicles,
// camera targets, HUD readouts and milestones and the ground reference are all
// built from the SCENE messages the sender broadcasts, so the level can be empty.
//
// Subclass it (C++ or Blueprint) to change the scene settings or to swap the
// procedural looks for your own meshes in OnVehicleSpawned.
UCLASS()
class UNREALROCKETVIZ_API AUrvLiveGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AUrvLiveGameMode();

	UPROPERTY(EditAnywhere, Category = "UnrealRocketViz")
	FString MulticastGroup = TEXT("239.255.76.86");

	UPROPERTY(EditAnywhere, Category = "UnrealRocketViz")
	int32 Port = 47686;

	// Cesium ion token; empty uses the project's default.
	UPROPERTY(EditAnywhere, Category = "UnrealRocketViz")
	FString IonAccessToken;

	// Imagery from a tile server instead of Cesium ion (see FUrvSceneSettings).
	UPROPERTY(EditAnywhere, Category = "UnrealRocketViz")
	FString ImageryUrlTemplate;

	// Printed small at the lower right, e.g. the imagery attribution.
	UPROPERTY(EditAnywhere, Category = "UnrealRocketViz")
	FString Credit;

	// Called once per entity, after its procedural look is built. Override to
	// rebuild it from your own meshes (the SCENE's "model" names the vehicle class).
	virtual void OnVehicleSpawned(AUrvVehicle* Vehicle, int32 EntityId, const FString& Model) {}

	AUrvDirector* GetDirector() const { return Director; }

protected:
	virtual void BeginPlay() override;

private:
	void ApplyScene(const FString& Json);

	UPROPERTY() TObjectPtr<AUrvDirector> Director;
	UPROPERTY() TObjectPtr<AUrvUdpReceiver> Receiver;
	UPROPERTY() TObjectPtr<AUrvSoundscape> Soundscape;
	UPROPERTY() TObjectPtr<AUrvPaths> Paths;
	UPROPERTY() TMap<int32, TObjectPtr<AUrvVehicle>> Vehicles;

	bool bGlobeReady = false;
	bool bGroundSet = false;
};
