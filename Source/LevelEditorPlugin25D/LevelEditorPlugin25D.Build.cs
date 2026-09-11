// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class LevelEditorPlugin25D : ModuleRules
{
	public LevelEditorPlugin25D(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"LevelEditorPlugin25D",
			"LevelEditorPlugin25D/Variant_Platforming",
			"LevelEditorPlugin25D/Variant_Platforming/Animation",
			"LevelEditorPlugin25D/Variant_Combat",
			"LevelEditorPlugin25D/Variant_Combat/AI",
			"LevelEditorPlugin25D/Variant_Combat/Animation",
			"LevelEditorPlugin25D/Variant_Combat/Gameplay",
			"LevelEditorPlugin25D/Variant_Combat/Interfaces",
			"LevelEditorPlugin25D/Variant_Combat/UI",
			"LevelEditorPlugin25D/Variant_SideScrolling",
			"LevelEditorPlugin25D/Variant_SideScrolling/AI",
			"LevelEditorPlugin25D/Variant_SideScrolling/Gameplay",
			"LevelEditorPlugin25D/Variant_SideScrolling/Interfaces",
			"LevelEditorPlugin25D/Variant_SideScrolling/UI"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
