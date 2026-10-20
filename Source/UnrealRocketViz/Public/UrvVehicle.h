#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UrvVehicle.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;

// Look of one stage, in metres. The actor origin is the point the state refers
// to; +X is the nose. The body spans X = StartX .. StartX + Length, and the
// nozzle exit (where the plume starts) sits at X = StartX - BellDiameter.
// With Meshes set, those meshes are drawn instead of the procedural body; Length,
// Diameter, NoseLength and Fins are then ignored, while StartX and
// BellDiameter still place the plume, so set them to match the mesh's nozzle.
USTRUCT(BlueprintType)
struct FUrvStageLook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere) double StartX = 0.0;
	UPROPERTY(EditAnywhere) double Length = 10.0;
	UPROPERTY(EditAnywhere) double Diameter = 2.0;
	UPROPERTY(EditAnywhere) double NoseLength = 0.0;     // cone on top, 0 = none
	UPROPERTY(EditAnywhere) int32 Fins = 0;
	UPROPERTY(EditAnywhere) double BellDiameter = 1.0;
	UPROPERTY(EditAnywhere) double PlumeLength = 10.0;   // at sea level
	// How much the plume lengthens once the air is gone (above about 40 km): 0.8 means +80 %.
	// Its root always stays inside the nozzle exit.
	UPROPERTY(EditAnywhere) double PlumeVacuumLength = 0.8;

	// false: draw no plume here and leave it to an OnEngine listener.
	UPROPERTY(EditAnywhere) bool bBuiltInPlume = true;

	// An imported model's base colours are multiplied by this (> 1 lifts a near-black vehicle that
	// would otherwise read as a silhouette in daylight). 1 = as modelled.
	UPROPERTY(EditAnywhere) double AlbedoGain = 1.0;

	// All placed with the same transform, so parts modelled in one frame stay assembled.
	UPROPERTY(EditAnywhere) TArray<TSoftObjectPtr<UStaticMesh>> Meshes;
	UPROPERTY(EditAnywhere) FVector MeshOffsetM = FVector::ZeroVector;      // mesh pivot in actor space [m]
	UPROPERTY(EditAnywhere) FRotator MeshRotation = FRotator::ZeroRotator;  // turns the meshes' axes onto +X = nose
	UPROPERTY(EditAnywhere) double MeshScale = 1.0;                          // on top of the meshes' own units
};

// Engine state for a stage, every frame: on/off and altitude [m].
DECLARE_MULTICAST_DELEGATE_TwoParams(FUrvEngineEvent, bool /*bOn*/, double /*AltitudeM*/);

UCLASS()
class UNREALROCKETVIZ_API AUrvVehicle : public AActor
{
	GENERATED_BODY()

public:
	AUrvVehicle();

	// Model: a name from SCENE "model" (e.g. "falcon9-class"). When Content/Vehicles/<Model>/ has
	// been imported (Tools/import_models.py) the vehicle is built from it; otherwise from Look.Meshes,
	// otherwise procedurally.
	void Build(const FUrvStageLook& InLook, const FString& InModel = FString());
	// The plume lengthens as the air thins. Also broadcasts OnEngine.
	void SetEngine(bool bOn, double AltitudeM);

	const FUrvStageLook& GetLook() const { return Look; }

	// Velocity relative to the ground [Unreal cm/s]. The director sets it with the pose.
	void SetVelocity(const FVector& InVelocityCmS) { VelocityCmS = InVelocityCmS; }
	FVector GetVelocity() const override { return VelocityCmS; }
	bool IsEngineOn() const { return bEngineOn; }
	double GetAltitudeM() const { return AltitudeM; }
	// Dynamic pressure [Pa]: the sender's value when it sends one (channel
	// "dynamic_pressure_kpa"), else an estimate from speed and an exponential atmosphere.
	double GetDynamicPressurePa() const;
	void SetSenderDynamicPressurePa(double Pa) { SenderDynamicPressurePa = Pa; }

	// Joint scalars, as powered-descent-sim's models/joints.py takes them (the reference):
	// legs and grid fins 0 = stowed .. 1 = deployed (the rest pose), fin deflection and engine
	// gimbal in degrees. No-op unless the vehicle was built from a model with a manifest.
	void SetJoints(double LegsDeploy, double FinsDeploy, double FinDeflectDeg, double GimbalPitchDeg, double GimbalYawDeg);

	// For a custom plume: attach it to this actor and drive it from here.
	FUrvEngineEvent OnEngine;

private:
	void BuildProceduralBody();
	bool BuildMeshBody();
	bool BuildFromModel(const FString& Model);
	void BuildPlume();
	void Paint(UStaticMeshComponent* C, const FLinearColor& Color, float Roughness);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FVector& CenterM, const FVector& SizeM, const FRotator& Rot);

	FUrvStageLook Look;
	FVector VelocityCmS = FVector::ZeroVector;
	bool bEngineOn = false;
	double AltitudeM = 0.0;
	double SenderDynamicPressurePa = -1.0;   // < 0: none

	UPROPERTY() TObjectPtr<USceneComponent> Root;
	UPROPERTY() TObjectPtr<USceneComponent> ModelRoot;                 // glTF body frame -> actor frame
	UPROPERTY() TMap<FName, TObjectPtr<USceneComponent>> Joints;       // by glTF node name
	TMap<FName, FTransform> RestLocal;                                  // their rest transforms
	TSharedPtr<class FJsonObject> Manifest;                             // <model>.json: joints and axes
	TArray<double> LastJoints;                                          // what SetJoints last applied
	UPROPERTY() TObjectPtr<UStaticMeshComponent> Plume;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> PlumeCore;   // short, hotter inner cone
	UPROPERTY() TObjectPtr<UPointLightComponent> Glow;
	UPROPERTY() TObjectPtr<UStaticMesh> Cylinder;
	UPROPERTY() TObjectPtr<UStaticMesh> Cone;
	UPROPERTY() TObjectPtr<UStaticMesh> Cube;
	UPROPERTY() TObjectPtr<UMaterialInterface> PlumeMaterial;
	UPROPERTY() TObjectPtr<UMaterialInterface> HullMaterial;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> PlumeMid;
	UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> CoreMid;
};
