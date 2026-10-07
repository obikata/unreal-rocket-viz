#pragma once

#include "CoreMinimal.h"

// State of one body at one instant. Frames: WGS84 ECEF, metres and seconds.
// QBody2Ecef rotates body vectors into ECEF (v_ecef = q * v_body); body +X is the nose.
struct FUrvEntityState
{
	int32 Id = 0;
	FVector PosEcef = FVector::ZeroVector;
	FVector VelEcef = FVector::ZeroVector;
	FQuat QBody2Ecef = FQuat::Identity;
	bool bEngineOn = false;
	uint32 EngineMask = 0;   // bit i: engine i burning
	// Display values from the sender, shown as they are (e.g. "speed_kmh").
	TMap<FName, double> Channels;
};

// Everything known at one simulation time. Bodies are matched across frames by Id.
struct FUrvFrame
{
	double SimTime = 0.0;
	double SendTime = 0.0;   // sender wall clock [s since 1970], 0 if unknown
	TArray<FUrvEntityState> Entities;

	const FUrvEntityState* Find(int32 Id) const
	{
		return Entities.FindByPredicate([Id](const FUrvEntityState& E) { return E.Id == Id; });
	}
};

// A discrete event as the sender named it.
struct FUrvEvent
{
	double SimTime = 0.0;
	FString Name;            // e.g. "STAGE_SEP"
	double ShownAt = 0.0;    // FPlatformTime::Seconds() when it reached the screen
};
