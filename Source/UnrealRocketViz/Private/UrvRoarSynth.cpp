#include "UrvRoarSynth.h"

namespace
{
	constexpr int32 Rate = 48000;
}

UUrvRoarSynth::UUrvRoarSynth(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetSampleRate(Rate);
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = false;
	SoundGroup = SOUNDGROUP_Default;
}

float UUrvRoarSynth::Noise()
{
	Seed ^= Seed << 13;
	Seed ^= Seed >> 17;
	Seed ^= Seed << 5;
	return float(Seed) * (2.0f / 4294967295.0f) - 1.0f;
}

int32 UUrvRoarSynth::OnGeneratePCMAudio(TArray<uint8>& OutAudio, int32 NumSamples)
{
	OutAudio.SetNumUninitialized(NumSamples * sizeof(int16));
	int16* Out = reinterpret_cast<int16*>(OutAudio.GetData());
	const float TG = Gain.load(), TFc = CutoffHz.load(), TCr = Crackle.load(), TRu = Rumble.load();
	const float Glide = 1.0f - FMath::Exp(-1.0f / (0.05f * Rate));   // 50 ms

	for (int32 i = 0; i < NumSamples; ++i)
	{
		G += (TG - G) * Glide;
		Fc += (TFc - Fc) * Glide;
		Cr += (TCr - Cr) * Glide;
		Ru += (TRu - Ru) * Glide;
		const float W = Noise();

		// Pink noise (Paul Kellet's filter): the broadband roar.
		Pink[0] = 0.99886f * Pink[0] + W * 0.0555179f;
		Pink[1] = 0.99332f * Pink[1] + W * 0.0750759f;
		Pink[2] = 0.96900f * Pink[2] + W * 0.1538520f;
		Pink[3] = 0.86650f * Pink[3] + W * 0.3104856f;
		Pink[4] = 0.55000f * Pink[4] + W * 0.5329522f;
		Pink[5] = -0.7616f * Pink[5] - W * 0.0168980f;
		const float Roar = (Pink[0] + Pink[1] + Pink[2] + Pink[3] + Pink[4] + Pink[5] + Pink[6] + W * 0.5362f) * 0.11f;
		Pink[6] = W * 0.115926f;

		// Crackle: sparse sharp bursts with a heavy-tailed size distribution.
		if (Noise() * 0.5f + 0.5f < Cr * 70.0f / Rate)
		{
			const float U = Noise() * 0.5f + 0.5f;
			BurstAmp = 0.4f + 2.6f * U * U * U;
			Burst = 1.0f;
		}
		Burst *= 0.9986f;   // ~15 ms
		const float Hp = W - BurstPrev;
		BurstPrev = W;
		const float Crack = Hp * Burst * BurstAmp;

		// Rumble: brown noise below ~100 Hz, swelling and fading a few times a second.
		Brown = 0.998f * Brown + W * 0.04f;
		RumbleLp += (Brown - RumbleLp) * 0.012f;
		if (--ThrobLeft <= 0)
		{
			ThrobTarget = 0.6f + 0.4f * Noise();
			ThrobLeft = Rate / 6;
		}
		Throb += (ThrobTarget - Throb) * 0.0004f;
		const float Low = RumbleLp * 4.0f * Throb;

		// Air absorption on everything that is not already low.
		const float A = 1.0f - FMath::Exp(-2.0f * PI * FMath::Clamp(Fc, 40.0f, 18000.0f) / Rate);
		const float Mid = Roar + Crack;
		Lp1 += (Mid - Lp1) * A;
		Lp2 += (Lp1 - Lp2) * A;

		const float Mix = (Lp2 + Low * Ru) * G;
		Out[i] = int16(FMath::Clamp(FMath::Tanh(Mix * 1.5f) * 30000.0f, -32767.0f, 32767.0f));
	}
	return NumSamples;
}
