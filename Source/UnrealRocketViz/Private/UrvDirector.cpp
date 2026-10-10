#include "UrvDirector.h"

#include "Cesium3DTileset.h"
#include "CesiumGeoreference.h"
#include "CesiumSampleHeightResult.h"
#include "EngineUtils.h"
#include "CesiumWgs84Ellipsoid.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UrvVehicle.h"

namespace
{
	constexpr int32 MaxBuffer = 512;
	constexpr double MaxLag = 0.5;   // [s] resync the display clock beyond this
	constexpr double StallMin = 0.25;   // [s] no frame for max(this, 3 frame intervals): paused or ended

	double UnixNow() { return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds(); }
}

AUrvDirector::AUrvDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

void AUrvDirector::Bind(int32 EntityId, AUrvVehicle* Vehicle, const FString& Label)
{
	Bindings.Add({EntityId, Vehicle, Label});
}

void AUrvDirector::PushFrame(const FUrvFrame& Frame)
{
	FScopeLock ScopeLock(&Lock);
	// Late frames are dropped; a jump back in time means a new run.
	if (Buffer.Num() > 0 && Frame.SimTime <= Buffer.Last().SimTime)
	{
		if (Frame.SimTime > Buffer.Last().SimTime - 1.0)
		{
			return;
		}
		Buffer.Reset();
	}
	Buffer.Add(Frame);
	const double Now = FPlatformTime::Seconds();
	if (LastPushAt > 0.0)
	{
		const double Gap = FMath::Min(Now - LastPushAt, 5.0);
		PushInterval = PushInterval > 0.0 ? 0.9 * PushInterval + 0.1 * Gap : Gap;
	}
	LastPushAt = Now;
	if (Buffer.Num() > MaxBuffer)
	{
		Buffer.RemoveAt(0, Buffer.Num() - MaxBuffer);
	}
}

void AUrvDirector::PushEvent(double SimTime, const FString& Name)
{
	FScopeLock ScopeLock(&Lock);
	PendingEvents.Add({SimTime, Name, 0.0});
}

FString AUrvDirector::GetLinkStatus() const
{
	FScopeLock ScopeLock(&Lock);
	return LinkStatus;
}

void AUrvDirector::SetLinkStatus(const FString& Text)
{
	FScopeLock ScopeLock(&Lock);
	LinkStatus = Text;
}

bool AUrvDirector::Sample(double T, FUrvFrame& Out) const
{
	if (Buffer.Num() == 0)
	{
		return false;
	}
	if (Buffer.Num() == 1 || T <= Buffer[0].SimTime)
	{
		Out = Buffer[0];
		return true;
	}
	if (T >= Buffer.Last().SimTime)
	{
		// Ran past the newest frame (a late packet): keep moving on the last velocity rather than stopping.
		Out = Buffer.Last();
		const double Dt = FMath::Min(T - Out.SimTime, MaxLag);
		for (FUrvEntityState& E : Out.Entities)
		{
			E.PosEcef += E.VelEcef * Dt;
		}
		Out.SimTime = T;
		return true;
	}
	int32 i = Buffer.Num() - 1;
	while (i > 0 && Buffer[i - 1].SimTime > T)
	{
		--i;
	}
	const FUrvFrame& A = Buffer[i - 1];
	const FUrvFrame& B = Buffer[i];
	const double K = (T - A.SimTime) / FMath::Max(B.SimTime - A.SimTime, 1e-9);
	Out = B;
	Out.SimTime = T;
	for (FUrvEntityState& E : Out.Entities)
	{
		if (const FUrvEntityState* Ea = A.Find(E.Id))
		{
			const FUrvEntityState* Eb = B.Find(E.Id);
			E.PosEcef = FMath::Lerp(Ea->PosEcef, Eb->PosEcef, K);
			E.VelEcef = FMath::Lerp(Ea->VelEcef, Eb->VelEcef, K);
			E.QBody2Ecef = FQuat::Slerp(Ea->QBody2Ecef, Eb->QBody2Ecef, K);
			for (TPair<FName, double>& C : E.Channels)
			{
				if (const double* Va = Ea->Channels.Find(C.Key))
				{
					C.Value = FMath::Lerp(*Va, C.Value, K);
				}
			}
		}
	}
	return true;
}

void AUrvDirector::Place(AUrvVehicle* V, const FUrvEntityState& E) const
{
	if (!V || !Georeference)
	{
		return;
	}
	const FVector Pos = Georeference->TransformEarthCenteredEarthFixedPositionToUnreal(E.PosEcef) + GroundOffset;
	// ECEF is right-handed and Unreal left-handed: the reflection turns body +Z into actor -Z.
	const FVector Nose = Georeference->TransformEarthCenteredEarthFixedDirectionToUnreal(E.QBody2Ecef.RotateVector(FVector::XAxisVector));
	const FVector BodyZ = Georeference->TransformEarthCenteredEarthFixedDirectionToUnreal(E.QBody2Ecef.RotateVector(FVector::ZAxisVector));
	V->SetActorLocationAndRotation(Pos, FRotationMatrix::MakeFromXZ(Nose, -BodyZ).ToQuat());
	const double Speed = E.VelEcef.Size();
	V->SetVelocity(Speed > 0.0
		? Georeference->TransformEarthCenteredEarthFixedDirectionToUnreal(E.VelEcef / Speed).GetSafeNormal() * Speed * 100.0
		: FVector::ZeroVector);
}

void AUrvDirector::SetGroundReference(const FVector& LonLatHeight, double AboveGroundM)
{
	bHasGroundRef = true;
	GroundRefLlh = LonLatHeight;
	GroundRefAboveM = AboveGroundM;
	GroundOffset = FVector::ZeroVector;
	bGroundFound = false;
	bGroundAsked = false;
}

void AUrvDirector::SampleGround(const FVector& WorldPos, TFunction<void(bool, const FVector&)> OnGround)
{
	TActorIterator<ACesium3DTileset> Terrain(GetWorld());
	if (!Terrain || !Georeference)
	{
		OnGround(false, WorldPos);
		return;
	}
	TWeakObjectPtr<ACesiumGeoreference> Geo = Georeference;
	Terrain->SampleHeightMostDetailed({ Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(WorldPos) },
		FCesiumSampleHeightMostDetailedCallback::CreateLambda(
			[Geo, WorldPos, OnGround](ACesium3DTileset*, const TArray<FCesiumSampleHeightResult>& R, const TArray<FString>&)
			{
				const bool bOk = Geo.IsValid() && R.Num() == 1 && R[0].SampleSuccess;
				OnGround(bOk, bOk ? Geo->TransformLongitudeLatitudeHeightPositionToUnreal(R[0].LongitudeLatitudeHeight) : WorldPos);
			}));
}

void AUrvDirector::SetGroundReferenceFromEntity(int32 EntityId, double AboveGroundM)
{
	SetGroundReference(FVector::ZeroVector, AboveGroundM);
	GroundRefEntity = EntityId;
}

void AUrvDirector::UpdateGroundOffset(double Now)
{
	if (!bHasGroundRef || !Georeference || bGroundAsked || bGroundFound || Now - GroundLastTry < 1.0)
	{
		return;
	}
	if (GroundRefEntity >= 0)
	{
		const FUrvEntityState* E = bHaveDisplay ? DisplayFrame.Find(GroundRefEntity) : nullptr;
		if (!E)
		{
			return;   // wait for the first frame carrying it
		}
		GroundRefLlh = UCesiumWgs84Ellipsoid::EarthCenteredEarthFixedToLongitudeLatitudeHeight(E->PosEcef);
		GroundRefEntity = -1;
	}
	GroundLastTry = Now;
	bGroundAsked = true;
	const FVector P = Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(GroundRefLlh);
	// The direction transform also scales metres to centimetres; keep only the direction.
	const FVector Up = Georeference->TransformEarthCenteredEarthFixedDirectionToUnreal(
		Georeference->TransformUnrealPositionToEarthCenteredEarthFixed(P).GetSafeNormal()).GetSafeNormal();
	TWeakObjectPtr<AUrvDirector> Self(this);
	SampleGround(P, [Self, P, Up](bool bOk, const FVector& Ground)
	{
		if (!Self.IsValid())
		{
			return;
		}
		Self->bGroundAsked = false;   // on failure, ask again a second later
		if (bOk)
		{
			Self->GroundOffset = Up * (((Ground - P) | Up) + Self->GroundRefAboveM * 100.0);
			Self->bGroundFound = true;
			UE_LOG(LogTemp, Log, TEXT("UnrealRocketViz: ground %.1f m from the reference"), ((Ground - P) | Up) / 100.0);
		}
	});
}

void AUrvDirector::DrawHud(const FUrvFrame& F) const
{
	if (!GEngine)
	{
		return;
	}
	int32 Key = 100;
	GEngine->AddOnScreenDebugMessage(Key++, 0.5f, FColor::White, FString::Printf(TEXT("T%+7.1f s"), F.SimTime));
	for (const FBinding& B : Bindings)
	{
		if (const FUrvEntityState* E = F.Find(B.Id))
		{
			const double Alt = UCesiumWgs84Ellipsoid::EarthCenteredEarthFixedToLongitudeLatitudeHeight(E->PosEcef).Z;
			GEngine->AddOnScreenDebugMessage(Key++, 0.5f, FColor::White, FString::Printf(
				TEXT("%-8s ALT %6.1f km  VEL %6.0f m/s%s"), *B.Label, Alt / 1000.0, E->VelEcef.Size(), E->bEngineOn ? TEXT("  ENGINE") : TEXT("")));
		}
	}
	GEngine->AddOnScreenDebugMessage(Key++, 0.5f, FColor::Orange, LastEvent.Name);
	GEngine->AddOnScreenDebugMessage(Key++, 0.5f, FColor(140, 160, 170),
		FString::Printf(TEXT("%s   LATENCY %4.0f ms"), *LinkStatus, LatencyMs));
}

void AUrvDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	FUrvFrame F;
	bool bHave = false;
	TArray<FUrvEvent> Events;
	{
		FScopeLock ScopeLock(&Lock);
		Events = MoveTemp(PendingEvents);
		PendingEvents.Reset();
		if (Buffer.Num() > 0)
		{
			const double Newest = Buffer.Last().SimTime;
			// Stalled: no frame for several of the feed's own intervals (a 1 Hz feed is not a stall).
			if (bClockRunning && FPlatformTime::Seconds() - LastPushAt > FMath::Max(StallMin, 3.0 * PushInterval))
			{
				// The stream paused or ended: settle on the newest frame. Extrapolating past it
				// and resyncing every MaxLag would bob a landed vehicle through the ground.
				Clock = FMath::Min(Clock + DeltaSeconds, Newest);
			}
			else
			{
				Clock += DeltaSeconds;
				if (!bClockRunning || FMath::Abs(Clock - (Newest - Delay)) > MaxLag)
				{
					Clock = Newest - Delay;
					bClockRunning = true;
				}
			}
			bHave = Sample(Clock, F);
			if (Buffer.Last().SendTime > 0.0)
			{
				LatencyMs = (UnixNow() - Buffer.Last().SendTime) * 1000.0;
			}
		}
	}
	for (const FUrvEvent& Ev : Events)
	{
		LastEvent = Ev;
		LastEvent.ShownAt = FPlatformTime::Seconds();
		bHaveEvent = true;
		UE_LOG(LogTemp, Log, TEXT("UnrealRocketViz: T%+.2f %s"), Ev.SimTime, *Ev.Name);
	}
	if (!bHave)
	{
		return;
	}

	for (const FBinding& B : Bindings)
	{
		if (const FUrvEntityState* E = F.Find(B.Id))
		{
			AUrvVehicle* V = B.Vehicle.Get();
			if (!V)
			{
				continue;
			}
			Place(V, *E);
			const double Alt = UCesiumWgs84Ellipsoid::EarthCenteredEarthFixedToLongitudeLatitudeHeight(E->PosEcef).Z;
			V->SetEngine(E->bEngineOn, Alt);
		}
	}
	UpdateGroundOffset(FPlatformTime::Seconds());
	DisplayFrame = F;
	bHaveDisplay = true;
	if (bDebugText)
	{
		FScopeLock ScopeLock(&Lock);
		DrawHud(F);
	}
}
