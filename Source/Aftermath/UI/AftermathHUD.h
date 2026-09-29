#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "AftermathHUD.generated.h"

class AAftermathCharacter;
class AAftermathPlayerController;
class UFont;
struct FAftermathInvRect;
struct FAftermathItemInstance;

/**
 * Grey-box HUD drawn with Canvas (no UMG assets needed yet):
 * crosshair, interaction prompt, stance/speed, vitals bars and ailment tags, hotbar,
 * and the Tab inventory panel (equip slots, cargo grids, nearby ground items, drag ghost).
 * Layout geometry comes from the controller so clicks and drawing always agree.
 */
UCLASS()
class AFTERMATH_API AAftermathHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void DrawCrosshairAndPrompt(const AAftermathCharacter& Char, UFont* Font);
	void DrawStatus(const AAftermathCharacter& Char, UFont* Font);
	void DrawVitals(const AAftermathCharacter& Char, UFont* Font);
	void DrawHotbar(const AAftermathCharacter& Char, const AAftermathPlayerController& PC, UFont* Font);
	void DrawInventory(const AAftermathCharacter& Char, const AAftermathPlayerController& PC, UFont* Font);
	void DrawItemBox(const FAftermathItemInstance& Item, const FAftermathInvRect& R, UFont* Font, float Alpha = 1.f);
	void DrawFrame(const FAftermathInvRect& R, const FLinearColor& Fill, const FLinearColor& Border);
	void DrawBar(float X, float Y, float W, float H, float Frac, const FLinearColor& Color, const FString& Label, UFont* Font);
	void DrawDeathScreen(UFont* Font);
	void DrawCombat(const AAftermathCharacter& Char, UFont* Font);
	void DrawTracers();
};
