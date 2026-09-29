#include "Debug/AftermathTestScript.h"

#include "Aftermath.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/AftermathDoor.h"
#include "Interaction/AftermathPickup.h"
#include "Items/AftermathInventoryComponent.h"
#include "Misc/CommandLine.h"
#include "Combat/AftermathCombatComponent.h"
#include "Player/AftermathPlayerController.h"
#include "Survival/AftermathSurvivalComponent.h"
#include "UnrealClient.h"
#include "Misc/Parse.h"
#include "Movement/AftermathMovementComponent.h"
#include "Player/AftermathCharacter.h"

UAftermathTestScript::UAftermathTestScript()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false);
}

FString UAftermathTestScript::GetRequestedScript()
{
#if UE_BUILD_SHIPPING
	return FString();
#else
	FString Name;
	FParse::Value(FCommandLine::Get(), TEXT("-AftermathScript="), Name);
	return Name;
#endif
}

void UAftermathTestScript::BeginPlay()
{
	Super::BeginPlay();
	ScriptName = GetRequestedScript();
	BuildScript();
	SortSteps();
}

void UAftermathTestScript::FaceTowards(AAftermathCharacter& Self, const FVector& Target)
{
	if (APlayerController* PC = Cast<APlayerController>(Self.GetController()))
	{
		// Aim from the real camera, since that's where the interaction trace starts.
		FVector ViewLoc;
		FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		PC->SetControlRotation((Target - ViewLoc).Rotation());
	}
}

AAftermathCharacter* UAftermathTestScript::FindOtherSurvivor(const AAftermathCharacter& Self) const
{
	for (TActorIterator<AAftermathCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != &Self)
		{
			return *It;
		}
	}
	return nullptr;
}

void UAftermathTestScript::BuildScript()
{
	Steps.Reset();

	auto Log = [this](const FString& Msg)
	{
		UE_LOG(LogAftermath, Display, TEXT("[AftermathTest] role=%s %s"), *ScriptName, *Msg);
	};

	auto NearestOfClass = [this](UClass* Cls, const FVector& From) -> AActor*
	{
		AActor* Best = nullptr;
		float BestD = TNumericLimits<float>::Max();
		for (TActorIterator<AActor> It(GetWorld(), Cls); It; ++It)
		{
			const float D = FVector::DistSquared(It->GetActorLocation(), From);
			if (D < BestD)
			{
				BestD = D;
				Best = *It;
			}
		}
		return Best;
	};

	if (ScriptName == TEXT("mover"))
	{
		// Spawned at StartA (yard, 4m south of HouseB door). Changes stance, then walks to HouseB and opens it.
		Steps.Add({ 2.f, TEXT("crouch"), [](AAftermathCharacter& C) { C.ToggleCrouchStance(); } });
		Steps.Add({ 5.f, TEXT("prone"), [](AAftermathCharacter& C) { C.ToggleProneStance(); } });
		Steps.Add({ 8.f, TEXT("stand"), [](AAftermathCharacter& C) { C.ToggleProneStance(); } });
		// Door is 9m ahead; 1.8s of jogging (4.5 m/s, ~0.3s ramp) stops ~1.2m short of it, inside interact range.
		Steps.Add({ 10.f, TEXT("walk_to_door"), [this](AAftermathCharacter& C) { HoldForwardUntil = 10.f + 1.8f; } });
		// First person so the view ray starts at the eye, matching the server's reach origin.
		Steps.Add({ 12.5f, TEXT("first_person"), [](AAftermathCharacter& C) { if (!C.IsFirstPerson()) { C.ToggleCameraMode(); } } });
		Steps.Add({ 13.f, TEXT("open_door"), [this, NearestOfClass, Log](AAftermathCharacter& C)
		{
			AActor* Door = NearestOfClass(AAftermathDoor::StaticClass(), C.GetActorLocation());
			if (!Door) { Log(TEXT("step=open_door result=no_door")); return; }
			FaceTowards(C, Cast<AAftermathDoor>(Door)->GetInteractLocation());
		} });
		Steps.Add({ 13.5f, TEXT("interact_door"), [Log](AAftermathCharacter& C)
		{
			AActor* Focus = C.GetFocusedInteractable();
			Log(FString::Printf(TEXT("step=interact_door focus=%s"), Focus ? *Focus->GetClass()->GetName() : TEXT("none")));
			C.TryInteract();
		} });
		// Walk through the now-open doorway to the loot 1m inside and pick it up with F.
		Steps.Add({ 16.f, TEXT("enter_house"), [this, NearestOfClass](AAftermathCharacter& C)
		{
			if (AActor* Loot = NearestOfClass(AAftermathPickup::StaticClass(), C.GetActorLocation()))
			{
				FaceTowards(C, Loot->GetActorLocation());
			}
			HoldForwardUntil = 16.f + 0.9f;
		} });
		Steps.Add({ 18.5f, TEXT("look_at_loot"), [this, NearestOfClass](AAftermathCharacter& C)
		{
			if (AActor* Loot = NearestOfClass(AAftermathPickup::StaticClass(), C.GetActorLocation()))
			{
				FaceTowards(C, Loot->GetActorLocation());
			}
		} });
		Steps.Add({ 19.f, TEXT("take_loot"), [Log](AAftermathCharacter& C)
		{
			AActor* Focus = C.GetFocusedInteractable();
			Log(FString::Printf(TEXT("step=take_loot focus=%s"), Focus ? *Focus->GetClass()->GetName() : TEXT("none")));
			C.TryInteract();
		} });

		// ---- Stage 2: survival + inventory through the real client->server RPCs ----
		// Drink from the bottle we just picked up (a sip = +200 water on the server, reported back via Vitals).
		Steps.Add({ 21.f, TEXT("drink"), [Log](AAftermathCharacter& C)
		{
			const UAftermathInventoryComponent* Inv = C.GetInventory();
			const int32 Uid = Inv ? Inv->FindFirstUid(TEXT("WaterBottle")) : 0;
			Log(FString::Printf(TEXT("step=drink uid=%d water_before=%.0f"), Uid, C.GetSurvival()->GetVitals().Water));
			C.RequestUseItem(Uid);
		} });
		// Eat the starting apple.
		Steps.Add({ 22.f, TEXT("eat"), [Log](AAftermathCharacter& C)
		{
			const int32 Uid = C.GetInventory()->FindFirstUid(TEXT("Apple"));
			Log(FString::Printf(TEXT("step=eat uid=%d energy_before=%.0f"), Uid, C.GetSurvival()->GetVitals().Energy));
			C.RequestUseItem(Uid);
		} });
		// Bandage with a rag when there's nothing to treat: server says so, rag is kept.
		Steps.Add({ 23.f, TEXT("bandage_unhurt"), [Log](AAftermathCharacter& C)
		{
			const int32 Uid = C.GetInventory()->FindFirstUid(TEXT("Rag"));
			Log(FString::Printf(TEXT("step=bandage_unhurt uid=%d rags=%d"), Uid, C.GetInventory()->CountOf(TEXT("Rag"))));
			C.RequestUseItem(Uid);
		} });
		// Open the inventory panel and take a screenshot for visual review.
		Steps.Add({ 24.f, TEXT("open_inventory"), [this](AAftermathCharacter& C)
		{
			if (AAftermathPlayerController* PC = Cast<AAftermathPlayerController>(C.GetController()))
			{
				PC->SetInventoryOpen(true);
			}
		} });
		Steps.Add({ 25.5f, TEXT("screenshot"), [](AAftermathCharacter&)
		{
			FScreenshotRequest::RequestScreenshot(TEXT("stage2_inventory"), true, false);
		} });
		// Drop one rag: server spawns a pickup the observer can see.
		Steps.Add({ 26.5f, TEXT("drop_rag"), [Log](AAftermathCharacter& C)
		{
			UAftermathInventoryComponent* Inv = C.GetInventory();
			const int32 Uid = Inv->FindFirstUid(TEXT("Rag"));
			Log(FString::Printf(TEXT("step=drop_rag uid=%d"), Uid));
			if (Inv->CountOf(TEXT("Rag")) > 1)
			{
				Inv->ServerSplit(Uid, 1);
			}
		} });
		Steps.Add({ 27.5f, TEXT("drop_rag_2"), [Log](AAftermathCharacter& C)
		{
			UAftermathInventoryComponent* Inv = C.GetInventory();
			// Drop the stack of exactly 1 (the split-off one if split worked, otherwise the only one).
			int32 Pick = 0;
			for (const FAftermathEquipSlot& S : Inv->GetSlots())
			{
				for (const FAftermathGridEntry& E : S.Cargo.Entries)
				{
					if (E.Item.Def == TEXT("Rag") && E.Item.Count == 1) { Pick = E.Item.Uid; }
				}
			}
			Log(FString::Printf(TEXT("step=drop_rag_2 uid=%d"), Pick));
			Inv->ServerDrop(Pick);
		} });
		Steps.Add({ 29.f, TEXT("close_inventory"), [this](AAftermathCharacter& C)
		{
			if (AAftermathPlayerController* PC = Cast<AAftermathPlayerController>(C.GetController()))
			{
				PC->SetInventoryOpen(false);
			}
		} });
		Steps.Add({ 30.f, TEXT("done"), [](AAftermathCharacter&) {} });
		}
		else if (ScriptName == TEXT("shooter"))
		{
		// Spawned at StartE (range, facing +X). Target spawns 10 m ahead at StartF.
		// Picks up the pistol, loads it, fires at the target through the real input path (trigger + RPC),
		// then reloads with R and finishes it off. Also sends one cheated shot (origin 9 m away) that the
		// server must refuse.
		auto TakeNearest = [this](AAftermathCharacter& C, FName Id)
		{
			AAftermathPickup* Best = nullptr;
			float BestD = TNumericLimits<float>::Max();
			for (TActorIterator<AAftermathPickup> It(GetWorld()); It; ++It)
			{
				const float D = FVector::Dist(It->GetActorLocation(), C.GetActorLocation());
				if (It->GetItemId() == Id && D < BestD) { Best = *It; BestD = D; }
			}
			if (Best)
			{
				C.GetInventory()->ServerTakeFromGround(Best, EAftermathTakeMode::Auto, EAftermathWearSlot::None, 0, 0, false);
			}
			return Best != nullptr;
		};
		Steps.Add({ 2.f, TEXT("take_pistol"), [TakeNearest, Log](AAftermathCharacter& C)
		{
			Log(FString::Printf(TEXT("step=take_pistol found=%d"), TakeNearest(C, TEXT("Pistol")) ? 1 : 0));
		} });
		// The range only has 5.45 magazines, so the test asks for two loaded 9mm mags through a dev-only
		// RPC (the server honours it only when started with -AftermathAllowDebug, never in Shipping).
		Steps.Add({ 3.f, TEXT("arm"), [this, Log](AAftermathCharacter& C)
		{
			C.DebugGiveLoadedMagazine(TEXT("Mag9mm"), 15);
			C.DebugGiveLoadedMagazine(TEXT("Mag9mm"), 15);
			Log(TEXT("step=arm"));
		} });
		Steps.Add({ 4.f, TEXT("load"), [Log](AAftermathCharacter& C)
		{
			const UAftermathInventoryComponent* Inv = C.GetInventory();
			const int32 Gun = Inv->FindFirstUid(TEXT("Pistol"));
			const int32 Mag = Inv->FindFirstUid(TEXT("Mag9mm"));
			Log(FString::Printf(TEXT("step=load gun=%d mag=%d"), Gun, Mag));
			C.RequestCombineItems(Mag, Gun);
		} });
		Steps.Add({ 5.f, TEXT("aim_at_target"), [this](AAftermathCharacter& C)
		{
			if (AAftermathCharacter* T = FindOtherSurvivor(C))
			{
				FaceTowards(C, T->GetActorLocation() + FVector(0, 0, 40.f));
			}
			if (UAftermathCombatComponent* Combat = C.GetCombat())
			{
				Combat->SetAiming(true);
			}
		} });
		// Six aimed shots, one every 0.5 s, re-aiming at the chest before each.
		for (int32 i = 0; i < 6; ++i)
		{
			const float At = 6.f + i * 0.5f;
			Steps.Add({ At, TEXT("shoot"), [this, Log, i](AAftermathCharacter& C)
			{
				if (AAftermathCharacter* T = FindOtherSurvivor(C))
				{
					FaceTowards(C, T->GetActorLocation() + FVector(0, 0, 30.f));
				}
				Log(FString::Printf(TEXT("step=shoot n=%d rounds=%d"), i, C.GetCombat()->GetHeldRounds()));
				C.GetCombat()->SetTriggerHeld(true);
			} });
			Steps.Add({ At + 0.1f, TEXT("release"), [](AAftermathCharacter& C) { C.GetCombat()->SetTriggerHeld(false); } });
		}
		// Cheated shot: muzzle claimed 9 m in front of the shooter ("shoot from next to the target").
		Steps.Add({ 9.5f, TEXT("cheat_shot"), [this, Log](AAftermathCharacter& C)
		{
			const FVector Fake = C.GetInteractEyeLocation() + C.GetActorForwardVector() * 900.f;
			Log(TEXT("step=cheat_shot"));
			C.DebugServerFire(Fake, C.GetActorForwardVector());
		} });
		Steps.Add({ 10.5f, TEXT("reload"), [Log](AAftermathCharacter& C)
		{
			Log(FString::Printf(TEXT("step=reload rounds_before=%d"), C.GetCombat()->GetHeldRounds()));
			C.GetCombat()->RequestReload();
		} });
		Steps.Add({ 13.f, TEXT("after_reload"), [Log](AAftermathCharacter& C)
		{
			Log(FString::Printf(TEXT("step=after_reload rounds=%d"), C.GetCombat()->GetHeldRounds()));
		} });
		Steps.Add({ 13.5f, TEXT("screenshot"), [](AAftermathCharacter&)
		{
			FScreenshotRequest::RequestScreenshot(TEXT("stage3_combat"), true, false);
		} });
		// Keep shooting until the target is dead (bounded).
		for (int32 i = 0; i < 10; ++i)
		{
			const float At = 14.f + i * 0.5f;
			Steps.Add({ At, TEXT("finish"), [this](AAftermathCharacter& C)
			{
				AAftermathCharacter* T = FindOtherSurvivor(C);
				if (T && !T->IsDead())
				{
					// Aim at the head.
					FaceTowards(C, T->GetActorLocation() + FVector(0, 0, 78.f));
					C.GetCombat()->SetTriggerHeld(true);
				}
			} });
			Steps.Add({ At + 0.1f, TEXT("release"), [](AAftermathCharacter& C) { C.GetCombat()->SetTriggerHeld(false); } });
		}
		Steps.Add({ 26.f, TEXT("done"), [](AAftermathCharacter&) {} });
		}
		else if (ScriptName == TEXT("target"))
		{
		// Spawned at StartF, 10 m in front of the shooter. Puts on the vest, stands still (so the test is
		// deterministic) and just reports what it experiences.
		Steps.Add({ 2.f, TEXT("wear_vest"), [this, Log](AAftermathCharacter& C)
		{
			AAftermathPickup* Vest = nullptr;
			for (TActorIterator<AAftermathPickup> It(GetWorld()); It; ++It)
			{
				if (It->GetItemId() == TEXT("PlateCarrier")) { Vest = *It; }
			}
			Log(FString::Printf(TEXT("step=wear_vest found=%d"), Vest ? 1 : 0));
			if (Vest)
			{
				C.GetInventory()->ServerTakeFromGround(Vest, EAftermathTakeMode::Auto, EAftermathWearSlot::None, 0, 0, false);
			}
		} });
		Steps.Add({ 26.f, TEXT("done"), [](AAftermathCharacter&) {} });
		}
		else if (ScriptName == TEXT("observer"))
	{
		// Spawned at StartC (start of the vault course, 60cm wall 3.85m ahead).
		// Tries to take a far-away pickup (must be refused), watches the mover, then vaults the wall.
		Steps.Add({ 15.f, TEXT("vault"), [this](AAftermathCharacter& C) { bVaultWhenPossible = true; HoldForwardUntil = 15.f + 3.f; } });
		Steps.Add({ 3.f, TEXT("cheat_far_pickup"), [this, NearestOfClass, Log](AAftermathCharacter& C)
		{
			// Pick the farthest pickup and ask the server directly, bypassing the client-side range check.
			AActor* Far = nullptr;
			float FarD = -1.f;
			for (TActorIterator<AAftermathPickup> It(GetWorld()); It; ++It)
			{
				const float D = FVector::Dist(It->GetActorLocation(), C.GetActorLocation());
				if (D > FarD) { FarD = D; Far = *It; }
			}
			Log(FString::Printf(TEXT("step=cheat_far_pickup dist=%.0f target=%s"), FarD, Far ? *Far->GetName() : TEXT("none")));
			if (Far)
			{
				// Same RPC a real client would send.
				C.DebugServerInteract(Far);
			}
		} });
		Steps.Add({ 30.f, TEXT("done"), [](AAftermathCharacter&) {} });
	}
}

void UAftermathTestScript::SortSteps()
{
	// The tick loop runs steps in array order, so they must be sorted by time.
	Steps.StableSort([](const FStep& A, const FStep& B) { return A.AtSeconds < B.AtSeconds; });
}

void UAftermathTestScript::LogObservations(AAftermathCharacter& Self)
{
	const UAftermathMovementComponent* Move = Self.GetAftermathMovement();
	FString Msg = FString::Printf(TEXT("t=%.1f self_stance=%s self_z=%.1f items=%d"),
		Elapsed, AftermathMath::StanceToString(Self.GetStance()), Self.GetActorLocation().Z,
		Self.GetInventory() ? Self.GetInventory()->CountItems() : 0);

	// Own vitals and gear as replicated to this (owning) client.
	if (const UAftermathSurvivalComponent* S = Self.GetSurvival())
	{
		const FAftermathVitals& V = S->GetVitals();
		Msg += FString::Printf(TEXT(" hp=%.0f water=%.0f energy=%.0f stamina=%.0f cuts=%d dead=%d"),
			V.Health, V.Water, V.Energy, V.Stamina, V.Cuts, V.bDead ? 1 : 0);
	}
	if (const UAftermathCombatComponent* Combat = Self.GetCombat())
	{
		Msg += FString::Printf(TEXT(" rounds=%d hitmarker_age=%.1f dmg_indicator_age=%.1f tracers=%d"),
			Combat->GetHeldRounds(), Combat->GetHitMarkerAge(), Combat->GetDamageIndicatorAge(), Combat->GetTracers().Num());
	}
	// Shot effects seen from the other player's gun (tracers fade in 0.25 s, so count them).
	if (const AAftermathCharacter* Other = FindOtherSurvivor(Self))
	{
		if (const UAftermathCombatComponent* OC = Other->GetCombat())
		{
			Msg += FString::Printf(TEXT(" other_shots_seen=%d"), OC->GetTracersReceived());
		}
	}
	if (const UAftermathInventoryComponent* Inv = Self.GetInventory())
	{
		Msg += FString::Printf(TEXT(" rags=%d bottle_uses=%d worn_torso=%d"),
			Inv->CountOf(TEXT("Rag")),
			Inv->FindItem(Inv->FindFirstUid(TEXT("WaterBottle"))) ? Inv->FindItem(Inv->FindFirstUid(TEXT("WaterBottle")))->Uses : -1,
			Inv->GetSlot(EAftermathWearSlot::Torso) && !Inv->GetSlot(EAftermathWearSlot::Torso)->IsEmpty() ? 1 : 0);
	}

	if (AAftermathCharacter* Other = FindOtherSurvivor(Self))
	{
		Msg += FString::Printf(TEXT(" other_stance=%s other_half=%.1f other_x=%.0f"),
			AftermathMath::StanceToString(Other->GetStance()),
			Other->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),
			Other->GetActorLocation().X);
		// Privacy check: another player's inventory and vitals must never reach this client.
		const UAftermathInventoryComponent* OtherInv = Other->GetInventory();
		const int32 OtherItems = OtherInv ? OtherInv->CountItems() : 0;
		if (OtherItems > 0)
		{
			Msg += FString::Printf(TEXT(" other_items=%d"), OtherItems);
		}
	}
	int32 Rags = 0;
	for (TActorIterator<AAftermathPickup> It(GetWorld()); It; ++It)
	{
		if (It->GetItemId() == TEXT("Rag")) { ++Rags; }
	}
	Msg += FString::Printf(TEXT(" ground_rags=%d"), Rags);

	for (TActorIterator<AAftermathDoor> It(GetWorld()); It; ++It)
	{
		if (It->IsOpen())
		{
			Msg += FString::Printf(TEXT(" door_open=%s"), *It->GetName());
		}
	}
	int32 Pickups = 0;
	for (TActorIterator<AAftermathPickup> It(GetWorld()); It; ++It)
	{
		++Pickups;
	}
	Msg += FString::Printf(TEXT(" pickups=%d"), Pickups);

	UE_LOG(LogAftermath, Display, TEXT("[AftermathTest] role=%s %s"), *ScriptName, *Msg);
}

void UAftermathTestScript::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	AAftermathCharacter* Self = Cast<AAftermathCharacter>(GetOwner());
	// The server unpossesses a dead body right away, so report our own death before the control check.
	if (bStarted && !bReportedDeath && Self && Self->IsDead())
	{
		bReportedDeath = true;
		UE_LOG(LogAftermath, Display, TEXT("[AftermathTest] role=%s step=observed_own_death t=%.1f"), *ScriptName, Elapsed);
	}
	if (ScriptName.IsEmpty() || !Self || !Self->IsLocallyControlled() || !Self->GetController())
	{
		return;
	}

	if (!bStarted)
	{
		bStarted = true;
		UE_LOG(LogAftermath, Display, TEXT("[AftermathTest] role=%s started steps=%d"), *ScriptName, Steps.Num());
	}

	Elapsed += DeltaTime;

	UAftermathMovementComponent* Move = Self->GetAftermathMovement();
	if (Elapsed < HoldForwardUntil && !(Move && Move->IsVaulting()))
	{
		Self->AddMovementInput(Self->GetActorForwardVector(), 1.f);
	}

	// Press space once, the first moment the client-side check says a vault is possible.
	if (bVaultWhenPossible && Move && Move->QueryVault().bCanVault)
	{
		bVaultWhenPossible = false;
		// Stop walking into the wall; the vault carries us over.
		HoldForwardUntil = Elapsed;
		UE_LOG(LogAftermath, Display, TEXT("[AftermathTest] role=%s step=press_vault t=%.1f x=%.0f"), *ScriptName, Elapsed, Self->GetActorLocation().X);
		Self->JumpOrVault();
	}

	while (NextStep < Steps.Num() && Elapsed >= Steps[NextStep].AtSeconds)
	{
		FStep& Step = Steps[NextStep++];
		UE_LOG(LogAftermath, Display, TEXT("[AftermathTest] role=%s step=%s t=%.1f"), *ScriptName, *Step.Name, Elapsed);
		Step.Action(*Self);
	}

	ObserveTimer -= DeltaTime;
	if (ObserveTimer <= 0.f)
	{
		ObserveTimer = 0.5f;
		LogObservations(*Self);
	}
}
