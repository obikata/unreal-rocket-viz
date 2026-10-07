#include "UrvScene.h"

#include "Cesium3DTileset.h"
#include "CesiumGeoreference.h"
#include "CesiumIonRasterOverlay.h"
#include "CesiumUrlTemplateRasterOverlay.h"
#include "CesiumSunSky.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"

namespace UrvScene
{
	ACesiumGeoreference* Setup(UWorld* World, AActor* Context, const FUrvSceneSettings& Settings)
	{
		// Vehicles fly hundreds of kilometres from the origin; do not let Unreal cull them.
		if (AWorldSettings* WS = World->GetWorldSettings())
		{
			WS->bEnableWorldBoundsChecks = false;
		}

		ACesiumGeoreference* Geo = ACesiumGeoreference::GetDefaultGeoreferenceForActor(Context);
		Geo->SetOriginPlacement(EOriginPlacement::CartographicOrigin);
		Geo->SetOriginLongitudeLatitudeHeight(Settings.OriginLonLatHeight);

		if (!TActorIterator<ACesium3DTileset>(World))
		{
			ACesium3DTileset* Terrain = World->SpawnActor<ACesium3DTileset>();
			Terrain->SetTilesetSource(ETilesetSource::FromCesiumIon);
			Terrain->SetIonAssetID(Settings.TerrainAssetId);
			if (!Settings.IonAccessToken.IsEmpty())
			{
				Terrain->SetIonAccessToken(Settings.IonAccessToken);
			}
			UCesiumRasterOverlay* Imagery = nullptr;
			if (!Settings.ImageryUrlTemplate.IsEmpty())
			{
				UCesiumUrlTemplateRasterOverlay* Url = NewObject<UCesiumUrlTemplateRasterOverlay>(Terrain, TEXT("Imagery"));
				Url->TemplateUrl = Settings.ImageryUrlTemplate;
				Url->MinimumLevel = Settings.ImageryMinimumLevel;
				Url->MaximumLevel = Settings.ImageryMaximumLevel;
				Imagery = Url;
			}
			else
			{
				UCesiumIonRasterOverlay* Ion = NewObject<UCesiumIonRasterOverlay>(Terrain, TEXT("Imagery"));
				Ion->IonAssetID = Settings.ImageryAssetId;
				Ion->IonAccessToken = Settings.IonAccessToken;
				Imagery = Ion;
			}
			Terrain->AddInstanceComponent(Imagery);
			Imagery->RegisterComponent();
			Imagery->AddToTileset();
		}
		if (!TActorIterator<ACesiumSunSky>(World))
		{
			ACesiumSunSky* Sky = World->SpawnActor<ACesiumSunSky>();
			Sky->TimeZone = Settings.TimeZone;
			Sky->UseDaylightSavingTime = false;
			Sky->SolarTime = Settings.SolarTime;
			Sky->UpdateSun();
		}
		return Geo;
	}
}
