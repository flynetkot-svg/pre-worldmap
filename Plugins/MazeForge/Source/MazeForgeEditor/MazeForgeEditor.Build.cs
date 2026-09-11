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

			// FlushRenderingCommands in MazeLevelExporter: batched export unloads
			// finished room packages, and pending render commands may still hold
			// their mesh buffers. Declared in a header, but lives in RenderCore.
			"RenderCore"
		});
	}
}
