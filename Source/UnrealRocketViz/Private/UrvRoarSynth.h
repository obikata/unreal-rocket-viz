#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWaveProcedural.h"
#include <atomic>
#include "UrvRoarSynth.generated.h"

// Rocket noise synthesised on the audio thread: broadband roar, the crackle of
// shock-laden exhaust, and a low rumble, through a low-pass that models air
// absorption. Mono, 48 kHz. Set the targets from the game thread; the synth
// glides to them.
UCLASS()
class UUrvRoarSynth : public USoundWaveProcedural
{
	GENERATED_BODY()

public:
	UUrvRoarSynth(const FObjectInitializer& ObjectInitializer);

	std::atomic<float> Gain{0.0f};      // linear
	std::atomic<float> CutoffHz{8000.0f};
	std::atomic<float> Crackle{0.0f};   // 0..1
	std::atomic<float> Rumble{1.0f};    // 0..1

	// Cutoff vent: hiss from high-passed, band-passed white noise. Gain is linear (the envelope is set by the caller).
	std::atomic<float> VentGain{0.0f};
	std::atomic<float> VentCutoffHz{20000.0f};   // air absorption of the hiss

protected:
	virtual int32 OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples) override;

private:
	// RBJ biquad, transposed direct form II.
	struct FBiquad
	{
		float B0 = 1.0f, B1 = 0.0f, B2 = 0.0f, A1 = 0.0f, A2 = 0.0f, Z1 = 0.0f, Z2 = 0.0f;
		float Process(float X)
		{
			const float Y = B0 * X + Z1;
			Z1 = B1 * X - A1 * Y + Z2;
			Z2 = B2 * X - A2 * Y;
			return Y;
		}
	};
	static FBiquad MakeHighpass(float F0, float Q, float Rate);
	static FBiquad MakeBandpass(float F0, float Q, float Rate);

	float Noise();

	uint32 Seed = 0x9E3779B9u;
	float G = 0.0f, Fc = 8000.0f, Cr = 0.0f, Ru = 1.0f;   // glided parameters
	float Pink[7] = {};
	float Lp1 = 0.0f, Lp2 = 0.0f;   // air absorption, two poles
	float Brown = 0.0f, RumbleLp = 0.0f;
	float Burst = 0.0f, BurstAmp = 0.0f, BurstPrev = 0.0f;
	float Throb = 0.0f, ThrobTarget = 0.0f;
	int32 ThrobLeft = 0;
	FBiquad VentHp, VentBp;
	float Vg = 0.0f, VFc = 20000.0f, VentLp = 0.0f;   // glided vent parameters, absorption state
};
