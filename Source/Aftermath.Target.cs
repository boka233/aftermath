// Aftermath - standalone game target (client + listen server). Used for local testing.

using UnrealBuildTool;
using System.Collections.Generic;

public class AftermathTarget : TargetRules
{
	public AftermathTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		ApplySharedSettings(this);
	}

	// Shared by all Aftermath targets so build settings never drift apart.
	public static void ApplySharedSettings(TargetRules Target)
	{
		Target.DefaultBuildSettings = BuildSettingsVersion.V7;
		Target.IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		Target.ExtraModuleNames.Add("Aftermath");
	}
}
