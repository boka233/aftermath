#include "Interaction/AftermathPickup.h"

#include "Aftermath.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Items/AftermathInventoryComponent.h"
#include "Items/AftermathItemDatabase.h"
#include "Net/UnrealNetwork.h"
#include "Player/AftermathCharacter.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "AftermathPickup"

AAftermathPickup::AAftermathPickup()
{
	bReplicates = true;
	SetNetUpdateFrequency(5.f);
	PrimaryActorTick.bCanEverTick = false;
	// Loose items only matter to nearby players.
	SetNetCullDistanceSquared(FMath::Square(15000.f));

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// Blocks traces (so it can be looked at) but not pawns (so you can't stand on loot).
	Mesh->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
	}
	UpdateVisual();
}

void AAftermathPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAftermathPickup, Item);
}

void AAftermathPickup::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority() && !Item.IsValid())
	{
		SetItemById(ItemId, Count);
	}
	UpdateVisual();
}

void AAftermathPickup::SetItem(const FAftermathItemInstance& InItem, const FAftermathGrid* InCargo)
{
	Item = InItem;
	ItemId = InItem.Def;
	Count = InItem.Count;
	Cargo = InCargo ? *InCargo : FAftermathGrid();
	UpdateVisual();
}

void AAftermathPickup::SetItemById(FName InItemId, int32 InCount)
{
	const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get();
	const FAftermathItemDef* Def = DB ? DB->Find(InItemId) : nullptr;
	if (!Def)
	{
		UE_LOG(LogAftermath, Warning, TEXT("[Aftermath] Pickup %s: unknown item id %s"), *GetName(), *InItemId.ToString());
		return;
	}
	FAftermathItemInstance NewItem = AftermathItems::MakeInstance(*Def, InCount);
	FAftermathGrid NewCargo;
	if (Def->HasCargo())
	{
		NewCargo = FAftermathGrid(Def->Wear.Grid.X, Def->Wear.Grid.Y);
	}
	SetItem(NewItem, &NewCargo);
}

void AAftermathPickup::SetRemainingCount(int32 InCount)
{
	Item.Count = FMath::Max(1, InCount);
	Count = Item.Count;
	ForceNetUpdate();
}

void AAftermathPickup::OnRep_Item()
{
	UpdateVisual();
}

void AAftermathPickup::UpdateVisual()
{
	if (!Mesh)
	{
		return;
	}
	// Grey-box: a box scaled to the item's grid footprint (10cm per cell), so size reads at a glance.
	FVector Scale(0.25f);
	if (const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get())
	{
		if (const FAftermathItemDef* Def = DB->Find(Item.IsValid() ? Item.Def : ItemId))
		{
			// At least 20cm across so small items are still easy to look at and click.
			Scale = FVector(FMath::Max(0.2f, Def->Size.X * 0.1f), FMath::Max(0.2f, Def->Size.Y * 0.1f),
				FMath::Max(0.12f, 0.08f + 0.02f * FMath::Min(Def->Size.X, Def->Size.Y)));
		}
	}
	Mesh->SetRelativeScale3D(Scale);
}

FText AAftermathPickup::GetDisplayName() const
{
	const UAftermathItemDatabase* DB = UAftermathItemDatabase::Get();
	const FAftermathItemDef* Def = DB ? DB->Find(Item.Def) : nullptr;
	if (!Def)
	{
		return FText::FromName(Item.Def);
	}
	if (Item.Count > 1)
	{
		return FText::FromString(FString::Printf(TEXT("%s x%d"), *Def->Name, Item.Count));
	}
	return FText::FromString(Def->Name);
}

FText AAftermathPickup::GetInteractPrompt(const AAftermathCharacter* User) const
{
	return FText::Format(LOCTEXT("Take", "捡起 {0}"), GetDisplayName());
}

FVector AAftermathPickup::GetInteractLocation() const
{
	return GetActorLocation();
}

bool AAftermathPickup::CanInteract(const AAftermathCharacter* User) const
{
	return !bTaken && !IsActorBeingDestroyed() && Item.IsValid();
}

void AAftermathPickup::Interact(AAftermathCharacter* User)
{
	if (!HasAuthority() || !User || !CanInteract(User))
	{
		return;
	}
	if (UAftermathInventoryComponent* Inv = User->GetInventory())
	{
		Inv->TakeFromGround(this, EAftermathTakeMode::Auto);
	}
}

void AAftermathPickup::MarkTaken()
{
	if (bTaken)
	{
		return;
	}
	bTaken = true;
	Destroy();
}

#undef LOCTEXT_NAMESPACE
