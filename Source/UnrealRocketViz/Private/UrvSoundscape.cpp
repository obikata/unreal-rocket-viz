#include "UrvSoundscape.h"

#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "UrvChasePawn.h"
#include "UrvRoarSynth.h"
#include "UrvVehicle.h"

namespace
{
	constexpr double SoundCmPerM = 100.0;
	constexpr double KeepS = 180.0;   // history long enough for sound from about 60 km
	constexpr double VentEndS = 15.0;   // the hiss has faded to nothing by then

	// Vent hiss level 0..1 this long after the engine went off: sharp attack, plateau, slow decay, long tail.
	double VentEnvelope(double Age)
	{
		if (Age <= 0.0 || Age >= VentEndS)
		{
			return 0.0;
		}
		double G;
		if (Age < 0.08)
		{
			G = 0.35 * Age / 0.08;
		}
		else if (Age < 2.0)
		{
			G = FMath::Lerp(0.35, 0.30, (Age - 0.08) / (2.0 - 0.08));
		}
		else if (Age < 6.0)
		{
			G = FMath::Lerp(0.30, 0.18, (Age - 2.0) / 4.0);
		}
		else
		{
			G = 0.18 * FMath::Pow(0.001 / 0.18, (Age - 6.0) / (VentEndS - 6.0));
		}
		return G / 0.35;
	}
}

AUrvSoundscape::AUrvSoundscape()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;   // after the camera has moved
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Audio = CreateDefaultSubobject<UAudioComponent>(TEXT("Audio"));
	Audio->SetupAttachment(RootComponent);
	Audio->bAutoActivate = false;
	Audio->bIsUISound = true;   // not spatialised: delay and distance are applied here
	Audio->bAllowSpatialization = false;
}

void AUrvSoundscape::BeginPlay()
{
	Super::BeginPlay();
	Synth = NewObject<UUrvRoarSynth>(this);
	Audio->SetSound(Synth);
	Audio->Play();
}

void AUrvSoundscape::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Clock += DeltaSeconds;
	History.SetNum(Vehicles.Num());
	WasOn.SetNumZeroed(Vehicles.Num());
	for (int32 v = 0; v < Vehicles.Num(); ++v)
	{
		if (const AUrvVehicle* V = Vehicles[v])
		{
			if (WasOn[v] && !V->IsEngineOn())
			{
				Vents.Add({ Clock, V->GetActorLocation() });
			}
			WasOn[v] = V->IsEngineOn();
			TArray<FSample>& H = History[v];
			H.Add({ Clock, V->GetActorLocation(), V->IsEngineOn() });
			int32 Old = 0;
			while (Old < H.Num() - 1 && H[Old].Time < Clock - KeepS)
			{
				++Old;
			}
			H.RemoveAt(0, Old, EAllowShrinking::No);
		}
	}

	Vents.RemoveAll([this](const FVent& Vn) { return Clock - Vn.Time > KeepS + VentEndS; });

	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!Cam || !Synth)
	{
		return;
	}
	const FVector Ear = Cam->GetCameraLocation();
	const AUrvVehicle* Riding = Listener ? Listener->GetOnboardVehicle() : nullptr;

	float Gain = 0.0f, Cutoff = 300.0f, Crackle = 0.0f, Rumble = 1.0f;
	bool bHearing = false;
	if (Riding)
	{
		// Through the structure: rumble, plus wind noise growing with dynamic pressure.
		const double Q = FMath::Clamp(Riding->GetDynamicPressurePa() / 30000.0, 0.0, 1.5);
		Gain = Riding->IsEngineOn() ? float(0.45 + 0.35 * Q) : float(0.12 * Q);
		Cutoff = float(350.0 + 2500.0 * Q);
	}
	else
	{
		// Loudest vehicle as heard now: the state it was in when its noise left it.
		for (int32 v = 0; v < History.Num(); ++v)
		{
			const TArray<FSample>& H = History[v];
			for (int32 i = H.Num() - 1; i >= 0; --i)
			{
				const double Dm = (Ear - H[i].Pos).Size() / SoundCmPerM;
				if (Dm > SpeedOfSoundMS * (Clock - H[i].Time) && i > 0)
				{
					continue;   // that noise has not reached the camera yet
				}
				if (H[i].bOn)
				{
					const float G = float(FMath::Min(1.0, FMath::Pow(ReferenceM / FMath::Max(Dm, 1.0), 0.85)));
					if (G > Gain)
					{
						Gain = G;
						Cutoff = float(14000.0 / (1.0 + Dm / 350.0));
						Crackle = float(FMath::Clamp(1.2 - Dm / 12000.0, 0.0, 1.0));
					}
					bHearing = true;
				}
				break;
			}
		}
	}
	// The loudest vent as heard now: it started when its sound left, Dm / c ago.
	float VentG = 0.0f, VentCutoff = 20000.0f;
	for (const FVent& Vn : Vents)
	{
		const double Dm = (Ear - Vn.Pos).Size() / SoundCmPerM;
		const double Level = VentEnvelope(Clock - Vn.Time - Dm / SpeedOfSoundMS) * FMath::Min(1.0, FMath::Pow(ReferenceM / FMath::Max(Dm, 1.0), 0.85));
		if (Level > VentG)
		{
			VentG = float(Level);
			VentCutoff = float(14000.0 / (1.0 + Dm / 350.0));
		}
	}

	if (bHearing && !bWasHearing)
	{
		Kick = 1.0;
	}
	bWasHearing = bHearing;
	Kick *= FMath::Exp(-DeltaSeconds / 1.2);
	Shake = Riding ? 0.0 : FMath::Clamp(double(Gain) * (0.6 + 1.2 * Kick), 0.0, 1.5);

	Synth->Gain = Gain * Volume;
	Synth->CutoffHz = Cutoff;
	Synth->Crackle = Crackle;
	Synth->Rumble = Rumble;
	Synth->VentGain = VentG * VentVolume * Volume;
	Synth->VentCutoffHz = VentCutoff;
}
