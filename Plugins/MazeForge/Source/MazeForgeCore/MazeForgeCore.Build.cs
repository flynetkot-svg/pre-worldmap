using UnrealBuildTool;

public class MazeForgeCore : ModuleRules
{
	public MazeForgeCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		// UMazeGenerator_Image reads pixels through FImage: ImageCore provides the
		// container, ImageWrapper decodes PNG/JPEG/BMP/TGA behind FImageUtils.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageCore",
			"ImageWrapper"
		});

		// UMazeGenerator_Mesh reads the source FMeshDescription rather than LODResources:
		// on a Nanite mesh the LOD 0 render data is a decimated fallback, not the geometry
		// the designer authored. Editor-only, the runtime never voxelizes anything.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("MeshDescription");
		}
	}
}
