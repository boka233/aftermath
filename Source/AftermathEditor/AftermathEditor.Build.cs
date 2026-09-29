// Aftermath - editor-only module: map building helpers exposed to the Python build scripts.

using UnrealBuildTool;

public class AftermathEditor : ModuleRules
{
	public AftermathEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		bUseUnity = false;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Aftermath",
			"Landscape",
			"NavigationSystem",
			// Recast headers (RecastNavMeshGenerator.h pulls in Recast/Recast.h) for the navigation diagnostic.
			"Navmesh",
			"UnrealEd"
		});
	}
}
