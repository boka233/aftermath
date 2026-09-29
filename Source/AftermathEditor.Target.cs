// Aftermath - editor target.

using UnrealBuildTool;
using System.Collections.Generic;

public class AftermathEditorTarget : TargetRules
{
	public AftermathEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		AftermathTarget.ApplySharedSettings(this);
		// Editor-only: map building helpers for the Python build scripts.
		ExtraModuleNames.Add("AftermathEditor");
	}
}
