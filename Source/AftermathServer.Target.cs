// Aftermath - headless dedicated server target. Requires a source build of the engine.

using UnrealBuildTool;
using System.Collections.Generic;

[SupportedPlatforms(UnrealPlatformClass.Server)]
public class AftermathServerTarget : TargetRules
{
	public AftermathServerTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Server;
		AftermathTarget.ApplySharedSettings(this);

		// Keep checks in shipping server builds: authority bugs should crash loudly, not desync silently.
		bUseChecksInShipping = true;
	}
}
