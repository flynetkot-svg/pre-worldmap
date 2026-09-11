using UnrealBuildTool;

public class MazeForgeStreaming : ModuleRules
{
	public MazeForgeStreaming(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MazeForgeCore"
		});
	}
}
