#pragma once

#include "CoreMinimal.h"

class ACesiumGeoreference;
class UWorld;

struct FUrvSceneSettings
{
	FVector OriginLonLatHeight = FVector::ZeroVector;   // degrees, degrees, metres (WGS84)
	double SolarTime = 10.5;                            // local solar time [h]
	double TimeZone = 0.0;                              // [h]
	int64 TerrainAssetId = 1;                           // Cesium ion: Cesium World Terrain
	int64 ImageryAssetId = 2;                           // Cesium ion: Bing Maps Aerial
	FString IonAccessToken;                             // empty: the project's default token
};

// Puts a Cesium globe, sun and sky into the world unless the level already has them.
// Tiles stream from Cesium ion with the project's default token.
namespace UrvScene
{
	UNREALROCKETVIZ_API ACesiumGeoreference* Setup(UWorld* World, AActor* Context, const FUrvSceneSettings& Settings);
}
