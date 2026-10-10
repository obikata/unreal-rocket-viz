#include "UrvLiveGameMode.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UrvChasePawn.h"
#include "UrvDirector.h"
#include "UrvHud.h"
#include "UrvPaths.h"
#include "UrvSoundscape.h"
#include "UrvUdpReceiver.h"

namespace
{
	double Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, double Default)
	{
		double V = Default;
		return (O.IsValid() && O->TryGetNumberField(Key, V)) ? V : Default;
	}

	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FString& Default = FString())
	{
		FString V;
		return (O.IsValid() && O->TryGetStringField(Key, V)) ? V : Default;
	}

	TSharedPtr<FJsonObject> Obj(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TSharedPtr<FJsonObject>* V = nullptr;
		return (O.IsValid() && O->TryGetObjectField(Key, V)) ? *V : nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
		return (O.IsValid() && O->TryGetArrayField(Key, V)) ? V : nullptr;
	}

	FVector LonLatHeight(const TSharedPtr<FJsonObject>& O)
	{
		return FVector(Num(O, TEXT("lon"), 0.0), Num(O, TEXT("lat"), 0.0), Num(O, TEXT("height"), 0.0));
	}

	FUrvStageLook Look(const TSharedPtr<FJsonObject>& L)
	{
		FUrvStageLook K;
		K.StartX = Num(L, TEXT("start_x"), K.StartX);
		K.Length = Num(L, TEXT("length"), K.Length);
		K.Diameter = Num(L, TEXT("diameter"), K.Diameter);
		K.NoseLength = Num(L, TEXT("nose_length"), K.NoseLength);
		K.Fins = int32(Num(L, TEXT("fins"), K.Fins));
		K.BellDiameter = Num(L, TEXT("bell_diameter"), K.BellDiameter);
		K.PlumeLength = Num(L, TEXT("plume_length"), K.PlumeLength);
		K.PlumeVacuumLength = Num(L, TEXT("plume_vacuum_length"), K.PlumeVacuumLength);
		return K;
	}
}

AUrvLiveGameMode::AUrvLiveGameMode()
{
	DefaultPawnClass = AUrvChasePawn::StaticClass();
	HUDClass = AUrvHud::StaticClass();
}

void AUrvLiveGameMode::BeginPlay()
{
	Super::BeginPlay();
	UWorld* World = GetWorld();
	Director = World->SpawnActor<AUrvDirector>();
	Soundscape = World->SpawnActor<AUrvSoundscape>();
	Receiver = World->SpawnActor<AUrvUdpReceiver>();
	Receiver->Group = MulticastGroup;
	Receiver->Port = Port;
	Receiver->Director = Director;
	Paths = World->SpawnActor<AUrvPaths>();
	Paths->Director = Director;
	Receiver->Paths = Paths;
	Receiver->OnScene.AddUObject(this, &AUrvLiveGameMode::ApplyScene);
	Receiver->Start();
}

void AUrvLiveGameMode::ApplyScene(const FString& Json)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealRocketViz: unreadable SCENE"));
		return;
	}
	UWorld* World = GetWorld();

	// The globe, once: its origin is where the sender's scene happens.
	if (!bGlobeReady)
	{
		FUrvSceneSettings S;
		S.OriginLonLatHeight = LonLatHeight(Obj(Root, TEXT("origin")));
		S.SolarTime = Num(Root, TEXT("solar_time"), S.SolarTime);
		// solar_time is local solar time: without an explicit zone, use the origin's
		// solar meridian (lon / 15 h), or the sun would follow UTC.
		S.TimeZone = Num(Root, TEXT("time_zone"), S.OriginLonLatHeight.X / 15.0);
		S.IonAccessToken = IonAccessToken;
		S.ImageryUrlTemplate = ImageryUrlTemplate;
		Director->Georeference = UrvScene::Setup(World, this, S);
		bGlobeReady = true;
	}

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	AUrvChasePawn* Cam = PC ? Cast<AUrvChasePawn>(PC->GetPawn()) : nullptr;
	AUrvHud* Hud = PC ? Cast<AUrvHud>(PC->GetHUD()) : nullptr;
	if (Cam)
	{
		Cam->Director = Director;
		Cam->Soundscape = Soundscape;
		Cam->Paths = Paths;
		Soundscape->Listener = Cam;
	}
	if (Hud)
	{
		Hud->Director = Director;
		Hud->MissionName = Str(Root, TEXT("mission"), Hud->MissionName);
		Hud->Credit = Credit.IsEmpty() ? Hud->Credit : Credit;
	}

	// Entities: spawn the new ones; a known id keeps its actor.
	if (const auto* Ents = Arr(Root, TEXT("entities")))
	{
		for (const TSharedPtr<FJsonValue>& V : *Ents)
		{
			const TSharedPtr<FJsonObject> E = V->AsObject();
			const int32 Id = int32(Num(E, TEXT("id"), -1));
			if (Id < 0 || Vehicles.Contains(Id))
			{
				continue;
			}
			const FUrvStageLook L = Look(Obj(E, TEXT("look")));
			AUrvVehicle* Veh = World->SpawnActor<AUrvVehicle>();
			Veh->Build(L);
			Vehicles.Add(Id, Veh);
			const FString Label = Str(E, TEXT("label"), FString::Printf(TEXT("ENTITY %d"), Id));
			Director->Bind(Id, Veh, Label);
			Soundscape->Vehicles.Add(Veh);
			if (Cam)
			{
				Cam->Targets.Add(Veh);
				Cam->AimX.Add(L.StartX + 0.5 * L.Length);
			}
			if (Hud)
			{
				FUrvGaugeGroup G;
				G.EntityId = Id;
				G.Label = Str(E, TEXT("label_local"), Label);
				G.LabelEn = Label;
				Hud->Groups.Add(G);
			}
			OnVehicleSpawned(Veh, Id, Str(E, TEXT("model")));
			UE_LOG(LogTemp, Log, TEXT("UnrealRocketViz: entity %d '%s' (%.1f m x %.2f m)"), Id, *Label, L.Length, L.Diameter);
		}
	}

	if (Hud)
	{
		if (const auto* Rs = Arr(Root, TEXT("readouts")))
		{
			Hud->Readouts.Reset();
			for (const TSharedPtr<FJsonValue>& V : *Rs)
			{
				const TSharedPtr<FJsonObject> R = V->AsObject();
				FUrvReadout Out;
				Out.Channel = FName(*Str(R, TEXT("channel")));
				Out.Label = Str(R, TEXT("label"));
				Out.Unit = Str(R, TEXT("unit"));
				Out.Decimals = int32(Num(R, TEXT("decimals"), 0));
				Out.Max = Num(R, TEXT("max"), 0.0);
				Hud->Readouts.Add(Out);
			}
		}
		if (const auto* Ms = Arr(Root, TEXT("milestones")))
		{
			Hud->Milestones.Reset();
			for (const TSharedPtr<FJsonValue>& V : *Ms)
			{
				const TSharedPtr<FJsonObject> M = V->AsObject();
				FUrvMilestone Out;
				Out.Time = Num(M, TEXT("time"), 0.0);
				Out.Code = Str(M, TEXT("code"));
				Out.Name = Str(M, TEXT("name"), Out.Code);
				Out.NameEn = Str(M, TEXT("name_en"), Out.Code);
				Hud->Milestones.Add(Out);
			}
		}
	}

	// How the sender's PATHs and the flown trails are drawn.
	if (const auto* Ps = Arr(Root, TEXT("paths")))
	{
		Paths->Styles.Reset();
		for (const TSharedPtr<FJsonValue>& V : *Ps)
		{
			const TSharedPtr<FJsonObject> P = V->AsObject();
			FUrvPathStyle S;
			S.Name = Str(P, TEXT("name"));
			S.Label = Str(P, TEXT("label"), S.Name);
			const FString Hex = Str(P, TEXT("color"));
			if (!Hex.IsEmpty())
			{
				S.Color = FLinearColor(FColor::FromHex(Hex));
			}
			bool bDashed = false;
			if (P.IsValid() && P->TryGetBoolField(TEXT("dashed"), bDashed))
			{
				S.bDashed = bDashed;
			}
			S.Ghosts = FMath::Clamp(int32(Num(P, TEXT("ghosts"), S.Ghosts)), 0, 50);
			Paths->Styles.Add(S);
		}
	}
	if (const TSharedPtr<FJsonObject> Tr = Obj(Root, TEXT("trail")))
	{
		const FString Channel = Str(Tr, TEXT("channel"));
		Paths->TrailChannel = Channel.IsEmpty() ? NAME_None : FName(*Channel);
		Paths->TrailMax = Num(Tr, TEXT("max"), Paths->TrailMax);
	}

	// Stand the scene on the streamed terrain, once.
	if (!bGroundSet)
	{
		if (const TSharedPtr<FJsonObject> G = Obj(Root, TEXT("ground_ref")))
		{
			const double Above = Num(G, TEXT("above_m"), 0.0);
			const int32 Entity = int32(Num(G, TEXT("entity"), -1));
			if (Entity >= 0)
			{
				// Wherever the sender has that entity in its first frame stands Above over the terrain.
				Director->SetGroundReferenceFromEntity(Entity, Above);
			}
			else
			{
				Director->SetGroundReference(LonLatHeight(G), Above);
			}
			bGroundSet = true;
		}
	}
}
