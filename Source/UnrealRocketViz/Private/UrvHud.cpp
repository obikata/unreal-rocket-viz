#include "UrvHud.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "UrvChasePawn.h"
#include "SUrvHudOverlay.h"

int32 AUrvHud::GetFollowedIndex() const
{
	const AUrvChasePawn* Cam = Cast<AUrvChasePawn>(GetOwningPawn());
	return Cam ? Cam->GetTargetIndex() : 0;
}

void AUrvHud::ToggleOverlay()
{
	if (Overlay.IsValid())
	{
		const bool bShown = Overlay->GetVisibility() != EVisibility::Collapsed;
		Overlay->SetVisibility(bShown ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	}
}

void AUrvHud::BeginPlay()
{
	Super::BeginPlay();
	if (GEngine && GEngine->GameViewport)
	{
		Overlay = SNew(SUrvHudOverlay).Hud(this);
		GEngine->GameViewport->AddViewportWidgetContent(Overlay.ToSharedRef(), 10);
	}
}

void AUrvHud::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Overlay.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Overlay.ToSharedRef());
	}
	Overlay.Reset();
	Super::EndPlay(EndPlayReason);
}
