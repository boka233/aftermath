#include "UI/AftermathHUD.h"

#include "Combat/AftermathCombatComponent.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Interaction/AftermathPickup.h"
#include "Items/AftermathGrid.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemDatabase.h"
#include "Movement/AftermathMovementComponent.h"
#include "Player/AftermathCharacter.h"
#include "Player/AftermathPlayerController.h"
#include "Survival/AftermathSurvivalComponent.h"
#include "UI/AftermathInventoryLayout.h"

#define LOCTEXT_NAMESPACE "AftermathHUD"

namespace
{
	FText StanceText(EAftermathStance Stance)
	{
		switch (Stance)
		{
		case EAftermathStance::Crouched: return LOCTEXT("Crouched", "蹲");
		case EAftermathStance::Prone:    return LOCTEXT("Prone", "趴");
		default:                         return LOCTEXT("Standing", "站");
		}
	}

	const FAftermathItemDef* DefOf(FName Id)
	{
		const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get();
		return DB ? DB->Find(Id) : nullptr;
	}

	/** Colour per category so items read at a glance in the grey-box UI. */
	FLinearColor CategoryColor(FName Category)
	{
		static const TMap<FName, FLinearColor> Colors = {
			{ TEXT("Medical"),  FLinearColor(0.55f, 0.15f, 0.15f) },
			{ TEXT("Food"),     FLinearColor(0.45f, 0.35f, 0.10f) },
			{ TEXT("Drink"),    FLinearColor(0.12f, 0.30f, 0.50f) },
			{ TEXT("Tool"),     FLinearColor(0.30f, 0.30f, 0.32f) },
			{ TEXT("Ammo"),     FLinearColor(0.40f, 0.32f, 0.12f) },
			{ TEXT("Magazine"), FLinearColor(0.25f, 0.25f, 0.18f) },
			{ TEXT("Clothing"), FLinearColor(0.20f, 0.32f, 0.22f) },
		};
		const FLinearColor* C = Colors.Find(Category);
		return C ? *C : FLinearColor(0.25f, 0.25f, 0.25f);
	}

	const FLinearColor PanelFill(0.02f, 0.02f, 0.03f, 0.82f);
	const FLinearColor PanelBorder(0.5f, 0.5f, 0.5f, 0.6f);
	const FLinearColor CellFill(0.10f, 0.10f, 0.12f, 0.9f);
	const FLinearColor CellBorder(0.25f, 0.25f, 0.28f, 1.f);
	const FLinearColor TextDim(0.75f, 0.75f, 0.75f, 1.f);
}

void AAftermathHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}
	UFont* Font = GEngine ? GEngine->GetMediumFont() : nullptr;

	AAftermathPlayerController* PC = Cast<AAftermathPlayerController>(GetOwningPlayerController());
	const AAftermathCharacter* Char = Cast<AAftermathCharacter>(GetOwningPawn());
	if (!PC)
	{
		return;
	}
	if (!Char)
	{
		// Between death and respawn the controller has no pawn.
		DrawDeathScreen(Font);
		return;
	}

	PC->UpdateLayout(Canvas->ClipX, Canvas->ClipY);

	DrawTracers();
	if (!PC->IsInventoryOpen())
	{
		DrawCrosshairAndPrompt(*Char, Font);
		DrawCombat(*Char, Font);
	}
	DrawStatus(*Char, Font);
	DrawVitals(*Char, Font);
	DrawHotbar(*Char, *PC, Font);
	if (PC->IsInventoryOpen())
	{
		DrawInventory(*Char, *PC, Font);
	}

	if (PC->GetToastTimeLeft() > 0.f && !PC->GetToast().IsEmpty())
	{
		float W = 0.f, H = 0.f;
		GetTextSize(PC->GetToast(), W, H, Font, 1.2f);
		DrawText(PC->GetToast(), FLinearColor(1.f, 0.6f, 0.3f), (Canvas->ClipX - W) * 0.5f, Canvas->ClipY * 0.7f, Font, 1.2f);
	}

	// Key help, top-left.
	DrawText(LOCTEXT("Help", "WASD 移动  Shift 冲刺  C 蹲  Z 趴  空格 跳/翻越  F 交互  V 视角  Tab 背包  左键 开火/近战  右键 瞄准  R 换弹  1-9 快捷栏").ToString(),
		FLinearColor(1.f, 1.f, 1.f, 0.55f), 24.f, 20.f, Font, 1.f);
	if (PC->IsInventoryOpen())
	{
		DrawText(LOCTEXT("InvHelp", "左键拖动 移动/穿上/合并/丢弃   R 旋转   右键 使用   G 丢下   X 拆分   悬停按 1-9 设快捷").ToString(),
			FLinearColor(1.f, 0.9f, 0.5f, 0.8f), 24.f, 44.f, Font, 1.f);
	}
}

void AAftermathHUD::DrawDeathScreen(UFont* Font)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.7f), 0.f, 0.f, Canvas->ClipX, Canvas->ClipY);
	const FString Line = LOCTEXT("Dead", "你死了。正在重生……").ToString();
	float W = 0.f, H = 0.f;
	GetTextSize(Line, W, H, Font, 2.f);
	DrawText(Line, FLinearColor(0.8f, 0.1f, 0.1f), (Canvas->ClipX - W) * 0.5f, Canvas->ClipY * 0.45f, Font, 2.f);
}

void AAftermathHUD::DrawTracers()
{
	// Everyone's shots nearby: short yellow lines, red when they hit someone.
	for (TActorIterator<AAftermathCharacter> It(GetWorld()); It; ++It)
	{
		const UAftermathCombatComponent* Combat = It->GetCombat();
		if (!Combat)
		{
			continue;
		}
		for (const UAftermathCombatComponent::FTracer& T : Combat->GetTracers())
		{
			const FVector A = Project(T.From, false);
			const FVector B = Project(T.To, false);
			// Project() returns Z <= 0 for points behind the camera.
			if (A.Z <= 0.f || B.Z <= 0.f)
			{
				continue;
			}
			const float Alpha = FMath::Clamp(1.f - T.Age / 0.25f, 0.f, 1.f);
			const FLinearColor C = T.bHit ? FLinearColor(1.f, 0.2f, 0.2f, Alpha) : FLinearColor(1.f, 0.9f, 0.4f, Alpha * 0.8f);
			DrawLine(A.X, A.Y, B.X, B.Y, C, 1.5f);
		}
	}
}

void AAftermathHUD::DrawCombat(const AAftermathCharacter& Char, UFont* Font)
{
	const UAftermathCombatComponent* Combat = Char.GetCombat();
	if (!Combat)
	{
		return;
	}
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;

	// Hit marker: an X around the crosshair, red on a kill.
	if (Combat->GetHitMarkerAge() < 0.3f)
	{
		const float A = 1.f - Combat->GetHitMarkerAge() / 0.3f;
		const FLinearColor C = Combat->WasLastHitKill() ? FLinearColor(1.f, 0.15f, 0.15f, A) : FLinearColor(1.f, 1.f, 1.f, A);
		DrawLine(CX - 14.f, CY - 14.f, CX - 7.f, CY - 7.f, C, 2.f);
		DrawLine(CX + 14.f, CY - 14.f, CX + 7.f, CY - 7.f, C, 2.f);
		DrawLine(CX - 14.f, CY + 14.f, CX - 7.f, CY + 7.f, C, 2.f);
		DrawLine(CX + 14.f, CY + 14.f, CX + 7.f, CY + 7.f, C, 2.f);
	}

	// Damage direction: a red arc segment pointing where the shot came from.
	if (Combat->GetDamageIndicatorAge() < 1.5f)
	{
		const FVector ToSource = (Combat->GetLastDamageFrom() - Char.GetActorLocation()).GetSafeNormal2D();
		const FRotator View = GetOwningPlayerController() ? GetOwningPlayerController()->GetControlRotation() : FRotator::ZeroRotator;
		const FVector Local = FRotator(0.f, -View.Yaw, 0.f).RotateVector(ToSource);
		// Screen: forward = up, right = right.
		const FVector2D Dir(Local.Y, -Local.X);
		const float R = FMath::Min(Canvas->ClipX, Canvas->ClipY) * 0.18f;
		const float A = 1.f - Combat->GetDamageIndicatorAge() / 1.5f;
		const FVector2D P = FVector2D(CX, CY) + Dir.GetSafeNormal() * R;
		const FVector2D Side(-Dir.Y, Dir.X);
		const FVector2D S = Side.GetSafeNormal() * 24.f;
		DrawLine(P.X - S.X, P.Y - S.Y, P.X + S.X, P.Y + S.Y, FLinearColor(1.f, 0.1f, 0.1f, A), 5.f);
	}

	// Weapon + ammo, bottom right above the item weight line.
	const FAftermathItemDef* Held = Combat->GetHeldDef();
	FString Line;
	if (!Held)
	{
		Line = LOCTEXT("Fists", "徒手").ToString();
	}
	else if (Held->IsFirearm())
	{
		const int32 Rounds = Combat->GetHeldRounds();
		Line = FString::Printf(TEXT("%s  %d"), *Held->Name, Rounds);
		if (Combat->IsReloading())
		{
			Line += LOCTEXT("Reloading", "  换弹中…").ToString();
		}
		else if (Rounds == 0)
		{
			Line += LOCTEXT("PressR", "  [R] 换弹").ToString();
		}
	}
	else
	{
		Line = Held->Name;
	}
	float W = 0.f, H = 0.f;
	GetTextSize(Line, W, H, Font, 1.3f);
	DrawText(Line, FLinearColor(1.f, 0.95f, 0.8f), Canvas->ClipX - W - 30.f, Canvas->ClipY - 110.f, Font, 1.3f);
	if (Combat->IsAiming())
	{
		DrawText(LOCTEXT("Aiming", "瞄准").ToString(), FLinearColor(0.7f, 1.f, 0.7f), CX + 16.f, CY + 16.f, Font, 0.9f);
	}
}

void AAftermathHUD::DrawCrosshairAndPrompt(const AAftermathCharacter& Char, UFont* Font)
{
	const float CX = Canvas->ClipX * 0.5f;
	const float CY = Canvas->ClipY * 0.5f;
	const FLinearColor CrossColor(1.f, 1.f, 1.f, 0.8f);
	DrawLine(CX - 6.f, CY, CX + 6.f, CY, CrossColor, 1.5f);
	DrawLine(CX, CY - 6.f, CX, CY + 6.f, CrossColor, 1.5f);

	const FText Prompt = Char.GetFocusedPrompt();
	if (!Prompt.IsEmpty())
	{
		const FString Line = FString::Printf(TEXT("[F] %s"), *Prompt.ToString());
		float W = 0.f, H = 0.f;
		GetTextSize(Line, W, H, Font, 1.2f);
		DrawText(Line, FLinearColor(1.f, 0.9f, 0.4f), CX - W * 0.5f, CY + 24.f, Font, 1.2f);
	}
}

void AAftermathHUD::DrawStatus(const AAftermathCharacter& Char, UFont* Font)
{
	const UAftermathMovementComponent* Move = Char.GetAftermathMovement();
	if (!Move)
	{
		return;
	}
	FString Mode;
	if (Move->IsVaulting())       { Mode = LOCTEXT("Vaulting", "翻越").ToString(); }
	else if (Move->IsSprinting()) { Mode = LOCTEXT("Sprint", "冲刺").ToString(); }
	else if (Move->WantsWalk())   { Mode = LOCTEXT("Walk", "慢走").ToString(); }
	else                          { Mode = LOCTEXT("Jog", "慢跑").ToString(); }

	FString Status = FString::Printf(TEXT("%s | %s | %.1f m/s"),
		*StanceText(Move->GetStance()).ToString(), *Mode, Move->Velocity.Size2D() / 100.f);
	if (const UAftermathInventoryComponent* Inv = Char.GetInventory())
	{
		Status += FString::Printf(TEXT(" | %.1f kg"), Inv->TotalWeightGrams() / 1000.f);
	}
	DrawText(Status, FLinearColor::White, 24.f, Canvas->ClipY - 40.f, Font, 1.1f);
}

void AAftermathHUD::DrawBar(float X, float Y, float W, float H, float Frac, const FLinearColor& Color, const FString& Label, UFont* Font)
{
	DrawRect(FLinearColor(0.f, 0.f, 0.f, 0.6f), X, Y, W, H);
	DrawRect(Color, X + 1.f, Y + 1.f, (W - 2.f) * FMath::Clamp(Frac, 0.f, 1.f), H - 2.f);
	DrawText(Label, FLinearColor::White, X + W + 8.f, Y - 3.f, Font, 0.9f);
}

void AAftermathHUD::DrawVitals(const AAftermathCharacter& Char, UFont* Font)
{
	const UAftermathSurvivalComponent* S = Char.GetSurvival();
	if (!S)
	{
		return;
	}
	const FAftermathVitals& V = S->GetVitals();
	const FAftermathSurvivalTuning& T = S->Tuning;

	const float X = 24.f;
	float Y = Canvas->ClipY - 210.f;
	const float W = 150.f;
	const float H = 12.f;
	const float Step = 22.f;

	DrawBar(X, Y, W, H, V.Health / T.MaxHealth, FLinearColor(0.2f, 0.75f, 0.25f), FString::Printf(TEXT("%s %.0f"), *LOCTEXT("Health", "生命").ToString(), V.Health), Font); Y += Step;
	DrawBar(X, Y, W, H, V.Blood / T.MaxBlood, FLinearColor(0.75f, 0.1f, 0.1f), FString::Printf(TEXT("%s %.0f"), *LOCTEXT("Blood", "血液").ToString(), V.Blood), Font); Y += Step;
	DrawBar(X, Y, W, H, V.Energy / T.MaxEnergy, FLinearColor(0.8f, 0.55f, 0.1f), LOCTEXT("Energy", "能量").ToString(), Font); Y += Step;
	DrawBar(X, Y, W, H, V.Water / T.MaxWater, FLinearColor(0.2f, 0.5f, 0.9f), LOCTEXT("Water", "水分").ToString(), Font); Y += Step;
	const FLinearColor HeatColor = V.Heat < T.ColdThreshold ? FLinearColor(0.4f, 0.7f, 1.f) : (V.Heat > 0.8f ? FLinearColor(1.f, 0.4f, 0.2f) : FLinearColor(0.9f, 0.9f, 0.6f));
	DrawBar(X, Y, W, H, V.Heat, HeatColor, LOCTEXT("Heat", "体温").ToString(), Font); Y += Step;
	DrawBar(X, Y, W, H, V.Stamina / T.MaxStamina, V.bExhausted ? FLinearColor(0.5f, 0.5f, 0.5f) : FLinearColor(0.9f, 0.9f, 0.9f), LOCTEXT("Stamina", "体力").ToString(), Font);

	// Ailment tags (right of the bars).
	TArray<TPair<FString, FLinearColor>> Ailments;
	if (V.Cuts > 0)          { Ailments.Emplace(FString::Printf(TEXT("%s x%d"), *LOCTEXT("Bleeding", "流血").ToString(), V.Cuts), FLinearColor(1.f, 0.2f, 0.2f)); }
	if (V.bBrokenLegs)       { Ailments.Emplace(V.bSplinted ? LOCTEXT("Splinted", "腿骨折(已固定)").ToString() : LOCTEXT("Broken", "腿骨折").ToString(), FLinearColor(1.f, 0.6f, 0.2f)); }
	if (V.bSick)             { Ailments.Emplace(LOCTEXT("Sick", "感冒").ToString(), FLinearColor(0.6f, 0.9f, 0.4f)); }
	if (V.bPoisoned)         { Ailments.Emplace(LOCTEXT("Poisoned", "食物中毒").ToString(), FLinearColor(0.7f, 0.9f, 0.2f)); }
	if (V.Heat < T.ColdThreshold) { Ailments.Emplace(LOCTEXT("Cold", "寒冷").ToString(), FLinearColor(0.5f, 0.8f, 1.f)); }
	if (V.Energy < T.MaxEnergy * 0.15f) { Ailments.Emplace(LOCTEXT("Hungry", "饥饿").ToString(), FLinearColor(1.f, 0.7f, 0.2f)); }
	if (V.Water < T.MaxWater * 0.15f)   { Ailments.Emplace(LOCTEXT("Thirsty", "口渴").ToString(), FLinearColor(0.4f, 0.7f, 1.f)); }
	float TY = Canvas->ClipY - 210.f;
	for (const TPair<FString, FLinearColor>& Tag : Ailments)
	{
		DrawText(Tag.Key, Tag.Value, X + W + 110.f, TY, Font, 1.f);
		TY += Step;
	}
}

void AAftermathHUD::DrawFrame(const FAftermathInvRect& R, const FLinearColor& Fill, const FLinearColor& Border)
{
	DrawRect(Fill, R.X, R.Y, R.W, R.H);
	DrawLine(R.X, R.Y, R.X + R.W, R.Y, Border, 1.f);
	DrawLine(R.X, R.Y + R.H, R.X + R.W, R.Y + R.H, Border, 1.f);
	DrawLine(R.X, R.Y, R.X, R.Y + R.H, Border, 1.f);
	DrawLine(R.X + R.W, R.Y, R.X + R.W, R.Y + R.H, Border, 1.f);
}

void AAftermathHUD::DrawItemBox(const FAftermathItemInstance& Item, const FAftermathInvRect& R, UFont* Font, float Alpha)
{
	const FAftermathItemDef* Def = DefOf(Item.Def);
	FLinearColor Fill = Def ? CategoryColor(Def->Category) : FLinearColor(0.3f, 0.3f, 0.3f);
	Fill.A = 0.95f * Alpha;
	// Condition tints the border: pristine grey, ruined red.
	const float H = FMath::Clamp(Item.Health, 0.f, 1.f);
	const FLinearColor Border = FLinearColor::LerpUsingHSV(FLinearColor(0.9f, 0.1f, 0.1f), FLinearColor(0.7f, 0.7f, 0.7f), H) * FLinearColor(1, 1, 1, Alpha);
	DrawFrame(R, Fill, Border);

	FString Corner;
	if (Item.Count > 1) { Corner = FString::Printf(TEXT("x%d"), Item.Count); }
	else if (Def && Def->Uses > 0) { Corner = FString::Printf(TEXT("%d/%d"), Item.Uses, Def->Uses); }
	else if (Def && Def->IsMagazine()) { Corner = FString::Printf(TEXT("%d/%d"), Item.Loaded, Def->Magazine.Capacity); }
	float CornerW = 0.f, CornerH = 0.f;
	if (!Corner.IsEmpty())
	{
		GetTextSize(Corner, CornerW, CornerH, Font, 0.75f);
	}

	// Name top-left, count/uses bottom-right. In short boxes (low resolution) both share one line,
	// so the name gets trimmed to leave room for the number, which matters more.
	FString Name = Def ? Def->Name : Item.Def.ToString();
	float NameW = 0.f, NameH = 0.f;
	GetTextSize(Name, NameW, NameH, Font, 0.75f);
	const bool bOneLine = R.H < NameH + CornerH + 2.f;
	const float NameMaxW = (bOneLine && !Corner.IsEmpty()) ? R.W - CornerW - 8.f : R.W - 4.f;
	while (Name.Len() > 0 && NameW > NameMaxW)
	{
		Name.LeftChopInline(1);
		GetTextSize(Name, NameW, NameH, Font, 0.75f);
	}
	if (!Name.IsEmpty())
	{
		DrawText(Name, FLinearColor(1.f, 1.f, 1.f, Alpha), R.X + 3.f, R.Y + 2.f, Font, 0.75f);
	}
	if (!Corner.IsEmpty())
	{
		const float CY = bOneLine ? R.Y + 2.f : R.Y + R.H - CornerH - 1.f;
		DrawText(Corner, FLinearColor(1.f, 1.f, 0.7f, Alpha), R.X + R.W - CornerW - 3.f, CY, Font, 0.75f);
	}
	if (Item.Wetness > 0.3f)
	{
		DrawText(LOCTEXT("Wet", "湿").ToString(), FLinearColor(0.4f, 0.7f, 1.f, Alpha), R.X + 3.f, R.Y + R.H - 16.f, Font, 0.75f);
	}
}

void AAftermathHUD::DrawHotbar(const AAftermathCharacter& Char, const AAftermathPlayerController& PC, UFont* Font)
{
	const FAftermathInventoryLayout& L = PC.GetLayout();
	const UAftermathInventoryComponent* Inv = Char.GetInventory();
	const TArray<int32>& Uids = PC.GetHotbar();
	for (int32 i = 0; i < L.Hotbar.Num(); ++i)
	{
		const FAftermathInvRect& R = L.Hotbar[i];
		DrawFrame(R, FLinearColor(0.f, 0.f, 0.f, 0.5f), CellBorder);
		const FAftermathItemInstance* Item = (Inv && Uids.IsValidIndex(i) && Uids[i]) ? Inv->FindItem(Uids[i]) : nullptr;
		if (Item)
		{
			DrawItemBox(*Item, FAftermathInvRect{ R.X + 2.f, R.Y + 2.f, R.W - 4.f, R.H - 4.f }, Font, 0.9f);
		}
		DrawText(FString::FromInt(i + 1), FLinearColor(1.f, 1.f, 1.f, 0.7f), R.X + 2.f, R.Y - 16.f, Font, 0.8f);
	}
}

void AAftermathHUD::DrawInventory(const AAftermathCharacter& Char, const AAftermathPlayerController& PC, UFont* Font)
{
	const UAftermathInventoryComponent* Inv = Char.GetInventory();
	if (!Inv)
	{
		return;
	}
	const FAftermathInventoryLayout& L = PC.GetLayout();
	const FAftermathDragState& Drag = PC.GetDrag();

	// ---- Nearby (ground) column ----
	DrawFrame(L.NearbyPanel, PanelFill, PanelBorder);
	DrawText(LOCTEXT("Nearby", "附近（地上）").ToString(), FLinearColor::White, L.NearbyPanel.X + 6.f, L.NearbyPanel.Y + 4.f, Font, 1.f);
	for (const FAftermathInvNearbyRow& Row : L.Nearby)
	{
		const AAftermathPickup* P = Row.Pickup.Get();
		if (!P)
		{
			continue;
		}
		const bool bDragged = Drag.bActive && Drag.FromGround.Get() == P;
		DrawItemBox(P->GetItem(), Row.Rect, Font, bDragged ? 0.35f : 1.f);
	}
	if (L.Nearby.Num() == 0)
	{
		DrawText(LOCTEXT("NothingNearby", "没有东西").ToString(), TextDim, L.NearbyPanel.X + 6.f, L.NearbyPanel.Y + 30.f, Font, 0.9f);
	}

	// ---- Right column ----
	DrawFrame(L.Panel, PanelFill, PanelBorder);
	DrawText(FString::Printf(TEXT("%s   %.1f kg"), *LOCTEXT("Gear", "装备").ToString(), Inv->TotalWeightGrams() / 1000.f),
		FLinearColor::White, L.Panel.X + 10.f, L.Panel.Y + 4.f, Font, 1.f);

	for (const FAftermathInvSlotView& SV : L.Slots)
	{
		DrawFrame(SV.Rect, CellFill, CellBorder);
		const FAftermathEquipSlot* Slot = Inv->GetSlot(SV.Slot);
		if (Slot && !Slot->IsEmpty())
		{
			const bool bDragged = Drag.bActive && Drag.bFromWorn && Drag.FromSlot == SV.Slot;
			DrawItemBox(Slot->Item, FAftermathInvRect{ SV.Rect.X + 2.f, SV.Rect.Y + 2.f, SV.Rect.W - 4.f, SV.Rect.H - 4.f }, Font, bDragged ? 0.35f : 1.f);
		}
		else
		{
			DrawText(AftermathItems::WearSlotToString(SV.Slot), TextDim, SV.Rect.X + 4.f, SV.Rect.Y + SV.Rect.H * 0.4f, Font, 0.9f);
		}
	}

	for (const FAftermathInvGridView& GV : L.Grids)
	{
		const FAftermathGrid* G = Inv->GetGrid(GV.Container);
		if (!G)
		{
			continue;
		}
		const FAftermathEquipSlot* HolderSlot = Inv->GetSlot(GV.Container);
		const FAftermathItemDef* OwnerDef = HolderSlot ? DefOf(HolderSlot->Item.Def) : nullptr;
		DrawText(OwnerDef ? OwnerDef->Name : FString(AftermathItems::WearSlotToString(GV.Container)), TextDim, GV.Rect.X, GV.Rect.Y - 20.f, Font, 0.9f);

		for (int32 Y = 0; Y < GV.Rows; ++Y)
		{
			for (int32 X = 0; X < GV.Cols; ++X)
			{
				DrawFrame(FAftermathInvRect{ GV.Rect.X + X * L.Cell, GV.Rect.Y + Y * L.Cell, L.Cell, L.Cell }, CellFill, CellBorder);
			}
		}
		for (const FAftermathGridEntry& E : G->Entries)
		{
			const bool bDragged = Drag.bActive && !Drag.bFromWorn && Drag.Uid == E.Item.Uid;
			DrawItemBox(E.Item, L.ItemRect(GV, E.X, E.Y, FAftermathGrid::Footprint(E.Item, E.bRotated)), Font, bDragged ? 0.35f : 1.f);
		}
	}

	// ---- Drag ghost ----
	if (Drag.bActive)
	{
		const FAftermathItemDef* Def = DefOf(Drag.Def);
		const FAftermathGridSize Size = Def ? (Drag.bRotated ? Def->Size.Rotated() : Def->Size) : FAftermathGridSize(1, 1);

		EAftermathWearSlot Container;
		int32 CX, CY;
		bool bValid;
		if (PC.GetDragPreview(Container, CX, CY, bValid))
		{
			if (const FAftermathInvGridView* GV = L.FindGrid(Container))
			{
				const FAftermathInvRect R = L.ItemRect(*GV, CX, CY, Size);
				DrawRect(bValid ? FLinearColor(0.2f, 0.9f, 0.2f, 0.35f) : FLinearColor(0.9f, 0.2f, 0.2f, 0.35f), R.X, R.Y, R.W, R.H);
			}
		}
		const FVector2D M = PC.GetMouse();
		FAftermathItemInstance Ghost;
		Ghost.Def = Drag.Def;
		const float MX = static_cast<float>(M.X);
		const float MY = static_cast<float>(M.Y);
		DrawItemBox(Ghost, FAftermathInvRect{ MX - L.Cell * 0.5f, MY - L.Cell * 0.5f, Size.X * L.Cell, Size.Y * L.Cell }, Font, 0.7f);
	}
}

#undef LOCTEXT_NAMESPACE
