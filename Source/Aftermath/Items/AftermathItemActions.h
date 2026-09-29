#pragma once

#include "CoreMinimal.h"

class AAftermathCharacter;

/** Why an item action failed (for logs, HUD messages and tests). */
enum class EAftermathActionResult : uint8
{
	Ok,
	NotFound,
	NotUsable,
	NothingToTreat,
	NeedsTool,
	NoRoom,
	Invalid,
	Dead
};

/**
 * Server-side item use and combining. Pure rules over a character's inventory + survival components;
 * the RPC entry points live on AAftermathCharacter and just forward here.
 *
 *   Use(uid):             eat / drink / bandage / splint / medicine / wear / take in hands / open a can
 *   Combine(a, b):        tool + can -> opened can; cutting tool + clothing -> rags; ammo + magazine -> load;
 *                         magazine + gun -> attach
 *   Unload(mag):          rounds back into the inventory
 *   Attach/Detach/Reload: magazines in and out of guns
 */
namespace AftermathItemActions
{
	AFTERMATH_API EAftermathActionResult Use(AAftermathCharacter& Who, int32 Uid);
	AFTERMATH_API EAftermathActionResult Combine(AAftermathCharacter& Who, int32 SourceUid, int32 TargetUid);
	AFTERMATH_API EAftermathActionResult Unload(AAftermathCharacter& Who, int32 MagazineUid);

	/** Put a carried magazine into a gun (the previous magazine, if any, goes back to the inventory). */
	AFTERMATH_API EAftermathActionResult AttachMagazine(AAftermathCharacter& Who, int32 GunUid, int32 MagUid);
	/** Take the magazine out of a gun into the inventory. */
	AFTERMATH_API EAftermathActionResult DetachMagazine(AAftermathCharacter& Who, int32 GunUid);
	/** Swap in the fullest compatible magazine carried. NeedsTool = no better magazine available. */
	AFTERMATH_API EAftermathActionResult Reload(AAftermathCharacter& Who, int32 GunUid);

	AFTERMATH_API const TCHAR* ResultToString(EAftermathActionResult R);
}
