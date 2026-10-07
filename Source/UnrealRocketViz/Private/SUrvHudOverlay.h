#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Widgets/SLeafWidget.h"

class AUrvHud;

// Paints the overlay for AUrvHud with Slate: anti-aliased, at native
// resolution, with the plugin's own fonts.
class SUrvHudOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SUrvHudOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AUrvHud>, Hud)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }

private:
	TWeakObjectPtr<AUrvHud> Hud;
	TMap<uint8, FSlateFontInfo> Fonts;
	mutable double FirstFrameTime = -1.0;   // FPlatformTime seconds; drives the fade-in
};
