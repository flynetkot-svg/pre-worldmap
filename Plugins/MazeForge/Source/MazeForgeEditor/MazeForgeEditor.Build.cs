using UnrealBuildTool;

public class MazeForgeEditor : ModuleRules
{
	public MazeForgeEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MazeForgeCore",
			"MazeForgeStreaming"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"InputCore",
			"UnrealEd",
			"EditorFramework",
			"ToolMenus",
			"AssetDefinition",
			"PropertyEditor",
			"MeshDescription",
			"StaticMeshDescription",
			"AssetRegistry",
			"PhysicsCore",
			"Projects",
			"RenderCore",
			"WorkspaceMenuStructure"
		});
	}
}
