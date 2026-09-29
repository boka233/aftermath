#pragma once

#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "Items/AftermathItemTypes.h"
#include "AftermathItemDatabase.generated.h"

/**
 * All item definitions, loaded once per process from Content/Data/items.json.
 *
 * Why JSON and not a DataTable asset: items are edited as plain text (reviewable diffs, no editor needed),
 * and the same file is read by server, client and tests. The file is staged as a non-asset file
 * (DirectoriesToAlwaysStageAsUFS) so packaged builds read it from the pak.
 */
UCLASS()
class AFTERMATH_API UAftermathItemDatabase : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Process-wide instance (null before engine init). */
	static UAftermathItemDatabase* Get();

	/** Null if the id is unknown. */
	const FAftermathItemDef* Find(FName Id) const;

	/** Shorthand for Get()->Find(Id), null-safe. */
	static const FAftermathItemDef* FindDef(FName Id)
	{
		const UAftermathItemDatabase* DB = Get();
		return DB ? DB->Find(Id) : nullptr;
	}

	int32 Num() const { return Defs.Num(); }
	const TArray<FName>& GetAllIds() const { return Ids; }

	/**
	 * Parse and validate a JSON array of item defs. Returns false and fills OutErrors on any problem
	 * (duplicate id, bad size, OpensInto/ammo pointing at unknown items...). Replaces current contents only on success.
	 */
	bool LoadFromJsonString(const FString& Json, TArray<FString>& OutErrors);

	/** Path the database loads from. */
	static FString GetDefaultPath();

private:
	TMap<FName, FAftermathItemDef> Defs;
	TArray<FName> Ids;
};

/**
 * Item definition by id, or null. One shared copy: per-file anonymous-namespace helpers with the same
 * name collide when unity builds merge those files into one translation unit.
 */
inline const FAftermathItemDef* DefOf(FName Id)
{
	return UAftermathItemDatabase::FindDef(Id);
}
