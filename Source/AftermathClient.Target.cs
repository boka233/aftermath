// Aftermath - client-only target. Contains no server code paths (WITH_SERVER_CODE=0).

using UnrealBuildTool;
using System.Collections.Generic;

public class AftermathClientTarget : TargetRules
{
	public AftermathClientTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Client;
		AftermathTarget.ApplySharedSettings(this);
	}
}
