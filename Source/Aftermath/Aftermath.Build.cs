// Aftermath - main runtime module.

using UnrealBuildTool;

public class Aftermath : ModuleRules
{
	public Aftermath(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"NetCore",
			"Json",
			"JsonUtilities"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore"
		});

		// Iris replication support (enabled at runtime via net.Iris.UseIrisReplication in DefaultEngine.ini).
		SetupIrisSupport(Target);

		PublicIncludePaths.Add(ModuleDirectory);
	}
}
