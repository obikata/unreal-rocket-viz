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

	// All placed with the same transform, so parts modelled in one frame stay assembled.
	UPROPERTY(EditAnywhere) TArray<TSoftObjectPtr<UStaticMesh>> Meshes;
	UPROPERTY(EditAnywhere) FVector MeshOffsetM = FVector::ZeroVector;      // mesh pivot in actor space [m]
	UPROPERTY(EditAnywhere) FRotator MeshRotation = FRotator::ZeroRotator;  // turns the meshes' axes onto +X = nose
	UPROPERTY(EditAnywhere) double MeshScale = 1.0;                          // on top of the meshes' own units
};

UCLASS()
class UNREALROCKETVIZ_API AUrvVehicle : public AActor
{
	GENERATED_BODY()

public:
	AUrvVehicle();

	void Build(const FUrvStageLook& InLook);
	// The plume lengthens and fans out as the air thins.
	void SetEngine(bool bOn, double AltitudeM);

private:
	void BuildProceduralBody();
	bool BuildMeshBody();
	void BuildPlume();
	void Paint(UStaticMeshComponent* C, const FLinearColor& Color, float Roughness);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FVector& CenterM, const FVector& SizeM, const FRotator& Rot);

	FUrvStageLook Look;

	UPROPERTY() TObjectPtr<USceneComponent> Root;
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
