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
	// Imagery from a tile server instead, e.g. "https://example.com/{z}/{x}/{reverseY}.jpg"
	// (Web Mercator; Cesium's {y} counts from the south, {reverseY} from the north). Overrides ImageryAssetId when set.
	FString ImageryUrlTemplate;
	int32 ImageryMinimumLevel = 0;   // the server's coarsest level, if it has no whole-world tile
	int32 ImageryMaximumLevel = 18;
	FString IonAccessToken;                             // empty: the project's default token
};

// Puts a Cesium globe, sun and sky into the world unless the level already has them.
// Terrain streams from Cesium ion; imagery from ion or from a tile server.
namespace UrvScene
{
	UNREALROCKETVIZ_API ACesiumGeoreference* Setup(UWorld* World, AActor* Context, const FUrvSceneSettings& Settings);
}
