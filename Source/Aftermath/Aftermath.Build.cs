// Aftermath - main runtime module.

using UnrealBuildTool;

public class Aftermath : ModuleRules
{
	public Aftermath(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// Small module: compile each .cpp on its own. Unity blobs merge files so file-scope helpers
		// (anonymous namespaces, test-file `using namespace`) collide whenever the grouping shifts.
		bUseUnity = false;

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
			"SlateCore",
			// Server-side pathfinding for the infected (navmesh built at runtime on the server).
			"NavigationSystem"
		});

		// Iris replication support (enabled at runtime via net.Iris.UseIrisReplication in DefaultEngine.ini).
		SetupIrisSupport(Target);

		PublicIncludePaths.Add(ModuleDirectory);
	}
}
