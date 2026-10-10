#include "UrvPaths.h"

#include "CesiumGeoreference.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "UrvDirector.h"
#include "UrvTypes.h"

namespace
{
	FString Key(int32 Entity, const FString& Name) { return FString::Printf(TEXT("%d/%s"), Entity, *Name); }

	// 0 green, 0.5 amber, 1 red.
	FLinearColor Heat(double K)
	{
		K = FMath::Clamp(K, 0.0, 1.0);
		const FLinearColor G(0.20f, 0.85f, 0.40f), A(1.00f, 0.72f, 0.15f), R(1.00f, 0.25f, 0.20f);
		return K < 0.5 ? FLinearColor::LerpUsingHSV(G, A, float(K * 2.0)) : FLinearColor::LerpUsingHSV(A, R, float(K * 2.0 - 1.0));
	}
}

AUrvPaths::AUrvPaths()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the director has placed the vehicles and the camera moved
}

namespace
{
	const TCHAR* ModeName(EUrvPathMode M)
	{
		return M == EUrvPathMode::Current ? TEXT("CURRENT") : M == EUrvPathMode::WithGhosts ? TEXT("WITH EARLIER PLANS") : TEXT("HIDDEN");
	}

	void Notice(const FString& Text)
	{
		UE_LOG(LogTemp, Log, TEXT("UnrealRocketViz: %s"), *Text);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(0x55525650, 2.0f, FColor::White, Text);
		}
	}
}

void AUrvPaths::PushPath(uint32 SenderId, int32 EntityId, const FString& Name, uint32 Version, double SimTime,
	TArray<FVector> PointsEcef)
{
	FScopeLock ScopeLock(&Lock);
	if (SenderId != Sender)
	{
		Sender = SenderId;
		Pending.Reset();
		Latest.Reset();
		bSenderChanged = true;
	}
	const FString K = Key(EntityId, Name);
	if (const uint32* Have = Latest.Find(K))
	{
		if (Version <= *Have)
		{
			return;   // a repeat (sent again in case of loss) or a late one
		}
	}
	Latest.Add(K, Version);
	Pending.Add({EntityId, Name, Version, SimTime, MoveTemp(PointsEcef)});
}

void AUrvPaths::CyclePathMode()
{
	PathMode = static_cast<EUrvPathMode>((static_cast<uint8>(PathMode) + 1) % 3);
	Notice(FString::Printf(TEXT("PATHS: %s (%d shown)"), ModeName(PathMode), Shown.Num()));
}

void AUrvPaths::ToggleTrail()
{
	bShowTrail = !bShowTrail;
	int32 N = 0;
	for (const TPair<int32, TArray<FTrailPoint>>& Tr : Trails)
	{
		N += Tr.Value.Num();
	}
	Notice(FString::Printf(TEXT("TRAIL: %s (%d points)"), bShowTrail ? TEXT("ON") : TEXT("OFF"), N));
}

const FUrvPathStyle& AUrvPaths::StyleFor(const FString& Name) const
{
	static const FUrvPathStyle Default;
	const FUrvPathStyle* S = Styles.FindByPredicate([&Name](const FUrvPathStyle& X) { return X.Name == Name; });
	return S ? *S : Default;
}

void AUrvPaths::ClearShown()
{
	Shown.Reset();
	Trails.Reset();
	LastTrailT = -1e300;
}

void AUrvPaths::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FUrvFrame* F = Director ? Director->GetDisplayFrame() : nullptr;
	if (!F || !Director->Georeference)
	{
		return;
	}
	const double T = F->SimTime;
	if (T < LastDisplayT - 1.0)
	{
		ClearShown();   // the sender started a new run
	}
	LastDisplayT = T;

	// Paths whose time has come on the display clock.
	{
		FScopeLock ScopeLock(&Lock);
		if (bSenderChanged)
		{
			ClearShown();
			bSenderChanged = false;
		}
		for (int32 i = 0; i < Pending.Num();)
		{
			if (Pending[i].SimTime <= T)
			{
				UE_LOG(LogTemp, Verbose, TEXT("UnrealRocketViz: path '%s' v%u, %d points"), *Pending[i].Name, Pending[i].Version,
					Pending[i].Ecef.Num());
				if (Shown.Num() == 0)
				{
					UE_LOG(LogTemp, Log, TEXT("UnrealRocketViz: first path '%s' (%d points)"), *Pending[i].Name, Pending[i].Ecef.Num());
				}
				Shown.Add(MoveTemp(Pending[i]));
				Pending.RemoveAt(i);
			}
			else
			{
				++i;
			}
		}
	}
	// Keep the newest Ghosts + 1 per (entity, name).
	for (int32 i = Shown.Num() - 1; i >= 0; --i)
	{
		int32 Newer = 0;
		for (int32 j = i + 1; j < Shown.Num(); ++j)
		{
			Newer += (Shown[j].Entity == Shown[i].Entity && Shown[j].Name == Shown[i].Name) ? 1 : 0;
		}
		if (Newer > StyleFor(Shown[i].Name).Ghosts)
		{
			Shown.RemoveAt(i);
		}
	}

	// The flown trail, sampled from what is displayed.
	if (T < LastTrailT || T - LastTrailT >= TrailSpacing)
	{
		LastTrailT = T;
		for (const FUrvEntityState& E : F->Entities)
		{
			TArray<FTrailPoint>& Tr = Trails.FindOrAdd(E.Id);
			const double* V = TrailChannel.IsNone() ? nullptr : E.Channels.Find(TrailChannel);
			Tr.Add({E.PosEcef, V ? *V : 0.0, V != nullptr});
			if (Tr.Num() > TrailMaxPoints)
			{
				Tr.RemoveAt(0, Tr.Num() - TrailMaxPoints);
			}
		}
	}
	Draw();
}

void AUrvPaths::Draw()
{
	if (PathMode == EUrvPathMode::Hidden && !bShowTrail)
	{
		return;
	}
	ACesiumGeoreference* Geo = Director->Georeference;
	const FVector Offset = Director->GetGroundOffset();
	auto ToWorld = [Geo, &Offset](const FVector& Ecef) { return Geo->TransformEarthCenteredEarthFixedPositionToUnreal(Ecef) + Offset; };

	// World width of one pixel at distance D: constant on-screen line width.
	FVector Cam = FVector::ZeroVector;
	double PixelAt1 = 0.001;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->PlayerCameraManager)
		{
			Cam = PC->PlayerCameraManager->GetCameraLocation();
			FVector2D Viewport(1920.0, 1080.0);
			if (GEngine && GEngine->GameViewport)
			{
				GEngine->GameViewport->GetViewportSize(Viewport);
			}
			const double HalfFov = FMath::DegreesToRadians(PC->PlayerCameraManager->GetFOVAngle() * 0.5);
			PixelAt1 = 2.0 * FMath::Tan(HalfFov) / FMath::Max(Viewport.X, 1.0);
		}
	}
	// One-frame lines in the world's line batcher (redrawn every tick).
	UWorld* World = GetWorld();
	auto Segment = [World, &Cam, PixelAt1](const FVector& A, const FVector& B, const FLinearColor& C, double Px) {
		const double D = FVector::Dist(Cam, 0.5 * (A + B));
		DrawDebugLine(World, A, B, C.ToFColor(true), false, -1.0f, SDPG_World, float(Px * D * PixelAt1));
	};

	if (PathMode != EUrvPathMode::Hidden)
	{
		for (int32 i = 0; i < Shown.Num(); ++i)
		{
			const FPath& P = Shown[i];
			int32 Age = 0;   // 0: the newest of its kind
			for (int32 j = i + 1; j < Shown.Num(); ++j)
			{
				Age += (Shown[j].Entity == P.Entity && Shown[j].Name == P.Name) ? 1 : 0;
			}
			if (Age > 0 && PathMode != EUrvPathMode::WithGhosts)
			{
				continue;
			}
			const FUrvPathStyle& S = StyleFor(P.Name);
			const float Fade = Age == 0 ? 1.0f : 0.55f / float(Age + 1);
			const FLinearColor C = Age == 0 ? S.Color : FLinearColor::LerpUsingHSV(FLinearColor(0.05f, 0.06f, 0.08f), S.Color, Fade);
			const double Px = Age == 0 ? LinePixels : 0.6 * LinePixels;
			FVector Prev = FVector::ZeroVector;
			for (int32 k = 0; k < P.Ecef.Num(); ++k)
			{
				const FVector W = ToWorld(P.Ecef[k]);
				if (k > 0 && (!S.bDashed || k % 2 == 1))
				{
					Segment(Prev, W, C, Px);
				}
				Prev = W;
			}
		}
	}

	if (bShowTrail)
	{
		for (const TPair<int32, TArray<FTrailPoint>>& Tr : Trails)
		{
			for (int32 k = 1; k < Tr.Value.Num(); ++k)
			{
				const FTrailPoint& B = Tr.Value[k];
				const FLinearColor C = B.bHasValue && TrailMax > 0.0 ? Heat(B.Value / TrailMax) : FLinearColor(0.85f, 0.87f, 0.9f);
				Segment(ToWorld(Tr.Value[k - 1].Ecef), ToWorld(B.Ecef), C, 0.7 * LinePixels);
			}
		}
	}
}
