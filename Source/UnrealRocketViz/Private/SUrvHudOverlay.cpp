#include "SUrvHudOverlay.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Rendering/DrawElements.h"
#include "UrvDirector.h"
#include "UrvHud.h"

// Minimal overlay: one line at the bottom centre with speed, the mission clock
// and altitude, the mission name under it, and the latest event name above it
// for a few seconds. White type with a soft dark halo, no panels. Layout is in
// 1080p reference units scaled to the widget height; every element gets its
// own layer so paint order is code order.
namespace
{
	FLinearColor Hex(uint8 R, uint8 G, uint8 B, float A = 1.0f)
	{
		FLinearColor C = FLinearColor::FromSRGBColor(FColor(R, G, B));
		C.A = A;
		return C;
	}

	const FLinearColor Ink = Hex(0xF2, 0xF2, 0xEE, 0.94f);   // values
	const FLinearColor Soft = Hex(0xF2, 0xF2, 0xEE, 0.62f);  // labels

	constexpr double SafeY = 40.0;
	constexpr double EventHold = 4.0;   // seconds an event name stays up

	enum class EAlign { Left, Centre, Right };
	enum class EFace { Gothic, Num, NumM, Mono };

	double Smooth(double X)
	{
		X = FMath::Clamp(X, 0.0, 1.0);
		return X * X * (3.0 - 2.0 * X);
	}

	struct FPainter
	{
		FSlateWindowElementList& Out;
		const FGeometry& Geo;
		int32* Layer;
		const TMap<EFace, FSlateFontInfo>& Fonts;
		double S;
		float Alpha = 1.0f;

		int32 Next() const { return (*Layer)++; }

		FSlateFontInfo Font(EFace Face, double Size, int32 Tracking = 0) const
		{
			// Small type keeps a pixel floor so it survives 720p and downscaled streams.
			const double Floor = Face == EFace::Mono ? 8.0 : Face == EFace::Gothic ? 10.0 : 0.0;
			FSlateFontInfo F = Fonts.FindChecked(Face);
			F.Size = float(FMath::Max(Floor, Size * S));
			F.LetterSpacing = Tracking;
			return F;
		}

		FVector2D Measure(const FString& Str, const FSlateFontInfo& F) const
		{
			return FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Str, F);
		}

		void RawText(const FString& Str, const FSlateFontInfo& F, FVector2D TopLeft, FLinearColor C) const
		{
			C.A *= Alpha;
			const FPaintGeometry G = Geo.ToPaintGeometry(FVector2f(Measure(Str, F)), FSlateLayoutTransform(FVector2f(TopLeft)));
			// Soft dark halo, wide then tight, so white type reads over a white horizon.
			FSlateFontInfo B = F;
			B.OutlineSettings.OutlineSize = FMath::Max(2, FMath::RoundToInt(4.0 * S));
			B.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.22f * C.A);
			FSlateDrawElement::MakeText(Out, Next(), G, Str, B, ESlateDrawEffect::None, FLinearColor::Transparent);
			B.OutlineSettings.OutlineSize = FMath::Max(1, FMath::RoundToInt(1.5 * S));
			B.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.45f * C.A);
			FSlateDrawElement::MakeText(Out, Next(), G, Str, B, ESlateDrawEffect::None, FLinearColor::Transparent);
			FSlateDrawElement::MakeText(Out, Next(), G, Str, F, ESlateDrawEffect::None, C);
		}

		// Y is the vertical centre of the line. Returns the measured size.
		FVector2D Text(const FString& Str, EFace Face, double Size, double X, double Y, const FLinearColor& C,
			EAlign Align = EAlign::Left, int32 Tracking = 0) const
		{
			const FSlateFontInfo F = Font(Face, Size, Tracking);
			const FVector2D M = Measure(Str, F);
			const double Left = Align == EAlign::Left ? X : Align == EAlign::Right ? X - M.X : X - M.X / 2.0;
			RawText(Str, F, FVector2D(Left, Y - M.Y / 2.0), C);
			return M;
		}

		double FiguresWidth(const FString& Str, EFace Face, double Size) const
		{
			const FSlateFontInfo F = Font(Face, Size);
			const double Pitch = Measure(TEXT("0"), F).X;
			double Total = 0.0;
			for (TCHAR Ch : Str)
			{
				Total += FChar::IsDigit(Ch) ? Pitch : Measure(FString(1, &Ch), F).X;
			}
			return Total;
		}

		void Disc(double Cx, double Cy, double R, FLinearColor C) const
		{
			C.A *= Alpha;
			const FSlateRoundedBoxBrush Brush(FLinearColor::White, float(R));
			FSlateDrawElement::MakeBox(Out, Next(), Geo.ToPaintGeometry(FVector2f(2.0 * R, 2.0 * R), FSlateLayoutTransform(FVector2f(Cx - R, Cy - R))),
				&Brush, ESlateDrawEffect::None, C);
		}

		// Arc in degrees, clockwise from +X (screen Y points down).
		void Arc(double Cx, double Cy, double R, double FromDeg, double ToDeg, FLinearColor C, double Thick) const
		{
			C.A *= Alpha;
			TArray<FVector2f> Pts;
			const int32 N = FMath::Max(8, FMath::CeilToInt(FMath::Abs(ToDeg - FromDeg) / 3.0));
			for (int32 i = 0; i <= N; ++i)
			{
				const double A = FMath::DegreesToRadians(FMath::Lerp(FromDeg, ToDeg, double(i) / N));
				Pts.Add(FVector2f(Cx + R * FMath::Cos(A), Cy + R * FMath::Sin(A)));
			}
			FSlateDrawElement::MakeLines(Out, Next(), Geo.ToPaintGeometry(), Pts, ESlateDrawEffect::None, C, true, float(FMath::Max(1.0, Thick * S)));
		}

		// Digits on a fixed pitch so a changing value does not shuffle. Returns the width.
		double Figures(const FString& Str, EFace Face, double Size, double X, double Y, const FLinearColor& C, EAlign Align) const
		{
			const FSlateFontInfo F = Font(Face, Size);
			const FVector2D Zero = Measure(TEXT("0"), F);
			TArray<double> W;
			double Total = 0.0;
			for (TCHAR Ch : Str)
			{
				W.Add(FChar::IsDigit(Ch) ? Zero.X : Measure(FString(1, &Ch), F).X);
				Total += W.Last();
			}
			double Cur = Align == EAlign::Left ? X : Align == EAlign::Right ? X - Total : X - Total / 2.0;
			for (int32 i = 0; i < Str.Len(); ++i)
			{
				const FString One(1, &Str[i]);
				RawText(One, F, FVector2D(Cur + (W[i] - Measure(One, F).X) / 2.0, Y - Zero.Y / 2.0), C);
				Cur += W[i];
			}
			return Total;
		}
	};

	FString Grouped(double V, int32 Decimals)
	{
		FNumberFormattingOptions Fmt;
		Fmt.SetMinimumFractionalDigits(Decimals).SetMaximumFractionalDigits(Decimals).SetUseGrouping(true);
		return FText::AsNumber(V, &Fmt, FInternationalization::Get().GetInvariantCulture()).ToString();
	}

	FString Hms(double T)
	{
		const int32 Secs = FMath::FloorToInt(FMath::Abs(T));
		return FString::Printf(TEXT("%02d:%02d:%02d"), Secs / 3600, (Secs / 60) % 60, Secs % 60);
	}

	// A shade along the bottom edge so the readout sits on something.
	void Shade(FSlateWindowElementList& Out, const FGeometry& Geo, int32& Layer, const FVector2D& Size, double S)
	{
		const double Hs = 260.0 * S;
		TArray<FSlateGradientStop> Stops;
		Stops.Add(FSlateGradientStop(FVector2f(0.0f, 0.0f), FLinearColor(0, 0, 0, 0.0f)));
		Stops.Add(FSlateGradientStop(FVector2f(0.0f, float(Hs * 0.6)), FLinearColor(0, 0, 0, 0.14f)));
		Stops.Add(FSlateGradientStop(FVector2f(0.0f, float(Hs)), FLinearColor(0, 0, 0, 0.34f)));
		FSlateDrawElement::MakeGradient(Out, Layer++, Geo.ToPaintGeometry(FVector2f(Size.X, Hs), FSlateLayoutTransform(FVector2f(0.0f, float(Size.Y - Hs)))),
			Stops, Orient_Horizontal);
	}

	constexpr double GaugeR = 54.0;   // every readout is the same size, so they line up whatever the digits
	constexpr double ArcFrom = 135.0, ArcSweep = 270.0;

	// Round gauge: label, value and unit stacked and centred, with an arc for value / Max.
	void Gauge(const FPainter& P, const FUrvEntityState* E, const FUrvReadout& R, double X, double Y)
	{
		// A channel the sender does not provide reads "--": nothing is computed here.
		const double* V = E ? E->Channels.Find(R.Channel) : nullptr;
		const double Rad = GaugeR * P.S;
		P.Disc(X, Y, Rad, FLinearColor(0.0f, 0.0f, 0.0f, 0.28f));
		P.Arc(X, Y, Rad, 0.0, 360.0, FLinearColor(1.0f, 1.0f, 1.0f, 0.18f), 1.0);
		if (V && R.Max > 0.0)
		{
			const double K = FMath::Clamp(*V / R.Max, 0.0, 1.0);
			if (K > 0.001)
			{
				P.Arc(X, Y, Rad, ArcFrom, ArcFrom + ArcSweep * K, Ink, 2.0);
			}
		}
		// Spread over the disc: label near the top, value in the middle, unit near the bottom.
		P.Text(R.Label, EFace::Gothic, 10.0, X, Y - 31.0 * P.S, Soft, EAlign::Centre, 120);
		P.Figures(V ? Grouped(*V, R.Decimals) : TEXT("--"), EFace::NumM, 26.0, X, Y + 1.0 * P.S, Ink, EAlign::Centre);
		P.Text(R.Unit, EFace::Mono, 9.0, X, Y + 30.0 * P.S, Soft, EAlign::Centre, 150);
	}

	void Readouts(const FPainter& P, const AUrvHud& Hud, const FUrvFrame& F, int32 Followed, double W, double H, double Now)
	{
		const double Cx = W / 2.0, Y = H - (SafeY + 12.0 + GaugeR) * P.S;
		const bool bCount = F.SimTime < 0.0;
		// "T+" and the digits are centred together, so the clock is symmetric about Cx.
		const FString Prefix = bCount ? TEXT("T−") : TEXT("T+");
		const FString Digits = Hms(bCount ? FMath::CeilToDouble(-F.SimTime) : F.SimTime);
		const double Pw = P.Measure(Prefix, P.Font(EFace::Num, 16.0)).X, Gap = 8.0 * P.S;
		const double Dw = P.FiguresWidth(Digits, EFace::Num, 30.0);
		const double Left = Cx - (Pw + Gap + Dw) / 2.0;
		P.Text(Prefix, EFace::Num, 16.0, Left, Y + 2.0 * P.S, Soft, EAlign::Left);
		P.Figures(Digits, EFace::Num, 30.0, Left + Pw + Gap, Y, Ink, EAlign::Left);
		P.Text(Hud.MissionName, EFace::Gothic, 11.0, Cx, Y + 28.0 * P.S, Soft, EAlign::Centre, 200);
		const double Half = (Pw + Gap + Dw) / 2.0;

		const FUrvGaugeGroup* G = Hud.Groups.IsValidIndex(Followed) ? &Hud.Groups[Followed] : nullptr;
		const FUrvEntityState* E = G ? F.Find(G->EntityId) : nullptr;
		for (int32 i = 0; i < Hud.Readouts.Num(); ++i)
		{
			const FUrvReadout& R = Hud.Readouts[i];
			const double Dx = Half + (36.0 + GaugeR + (2.0 * GaugeR + 24.0) * (i / 2)) * P.S;
			Gauge(P, E, Hud.Readouts[i], i % 2 == 0 ? Cx - Dx : Cx + Dx, Y);
		}

		if (const FUrvEvent* Ev = Hud.Director ? Hud.Director->GetLastEvent() : nullptr)
		{
			const double Age = Now - Ev->ShownAt;
			if (Age >= 0.0 && Age < EventHold)
			{
				const FUrvMilestone* M = Hud.FindMilestone(Ev->Name);
				FPainter Q = P;
				Q.Alpha = P.Alpha * float(Smooth(Age / 0.3) * (1.0 - Smooth((Age - (EventHold - 0.8)) / 0.8)));
				Q.Text(M ? M->Name : Ev->Name, EFace::Gothic, 15.0, Cx, Y - 36.0 * P.S, Ink, EAlign::Centre, 300);
			}
		}
	}
}

void SUrvHudOverlay::Construct(const FArguments& InArgs)
{
	Hud = InArgs._Hud;
	SetVisibility(EVisibility::HitTestInvisible);

	FString Dir;
	if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UnrealRocketViz")))
	{
		Dir = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources/Fonts"));
	}
	auto Load = [&Dir](const TCHAR* File)
	{
		const TSharedPtr<const FCompositeFont> Font = MakeShared<FStandaloneCompositeFont>(
			FName(File), FPaths::Combine(Dir, File), EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		return FSlateFontInfo(Font, 12.0f);
	};
	Fonts.Add(static_cast<uint8>(EFace::Gothic), Load(TEXT("ZenKakuGothicNew-Medium.ttf")));
	Fonts.Add(static_cast<uint8>(EFace::Num), Load(TEXT("ChakraPetch-SemiBold.ttf")));
	Fonts.Add(static_cast<uint8>(EFace::NumM), Load(TEXT("ChakraPetch-Medium.ttf")));
	Fonts.Add(static_cast<uint8>(EFace::Mono), Load(TEXT("ShareTechMono-Regular.ttf")));
}

int32 SUrvHudOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const AUrvHud* H = Hud.Get();
	const FUrvFrame* Frame = H && H->Director ? H->Director->GetDisplayFrame() : nullptr;
	if (!Frame)
	{
		return LayerId;
	}
	const double Now = FPlatformTime::Seconds();
	if (FirstFrameTime < 0.0)
	{
		FirstFrameTime = Now;
	}
	const FVector2D Size = Geo.GetLocalSize();
	const double S = Size.Y / 1080.0;
	int32 Layer = LayerId;
	Shade(Out, Geo, Layer, Size, S);

	TMap<EFace, FSlateFontInfo> F;
	for (const TPair<uint8, FSlateFontInfo>& Kv : Fonts)
	{
		F.Add(static_cast<EFace>(Kv.Key), Kv.Value);
	}
	FPainter P{Out, Geo, &Layer, F, S};
	P.Alpha = float(Smooth((Now - FirstFrameTime) / 0.8));
	Readouts(P, *H, *Frame, H->GetFollowedIndex(), Size.X, Size.Y, Now);
	return Layer;
}
