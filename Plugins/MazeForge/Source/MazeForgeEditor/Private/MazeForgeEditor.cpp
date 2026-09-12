#include "MazeForgeEditor.h"

#include "Framework/Docking/TabManager.h"
#include "MazeForgeCore.h"
#include "Mode/MazeEdModeSettingsDetails.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "World/SMazeWorldMap.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "MazeForgeEditor"

namespace
{
	/** Class name without the U prefix — that is what the property system keys layouts by. */
	const FName MazeEdModeSettingsName(TEXT("MazeEdModeSettings"));
}

void FMazeForgeEditorModule::StartupModule()
{
	// Factories and asset definitions are picked up through reflection, so there is no need to
	// register them by hand.
	//
	// The details customization does have to be registered: it is what puts the surface swatch
	// row inside the Brush section of the mode panel, and the property system will not find it
	// on its own.
	FPropertyEditorModule& PropertyEditorModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	PropertyEditorModule.RegisterCustomClassLayout(
		MazeEdModeSettingsName,
		FOnGetDetailCustomizationInstance::CreateStatic(&FMazeEdModeSettingsDetails::MakeInstance));

	// The world map is a nomad tab and not an asset editor, and that is a workflow choice: it is
	// meant to sit open beside the maze editor while both are worked on, and an asset editor
	// window that has to be reopened every time the graph is touched would not.
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(SMazeWorldMap::TabId,
			FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
			{
				return SNew(SDockTab)
					.TabRole(ETabRole::NomadTab)
					[
						SNew(SMazeWorldMap)
					];
			}))
		.SetDisplayName(LOCTEXT("MazeWorldMapTab", "MazeForge World Map"))
		.SetTooltipText(LOCTEXT("MazeWorldMapTabTip",
			"How this game's mazes join up: the schematics, their transition points, and the "
			"links between them."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Levels"));

	UE_LOG(LogMazeForge, Log, TEXT("MazeForgeEditor started."));
}

void FMazeForgeEditorModule::ShutdownModule()
{
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SMazeWorldMap::TabId);

	// Fetched rather than loaded: on editor shutdown the property editor may already be gone, and
	// LoadModuleChecked would bring a dying module back to life just to unregister from it.
	if (FPropertyEditorModule* PropertyEditorModule =
		FModuleManager::GetModulePtr<FPropertyEditorModule>("PropertyEditor"))
	{
		PropertyEditorModule->UnregisterCustomClassLayout(MazeEdModeSettingsName);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMazeForgeEditorModule, MazeForgeEditor)
