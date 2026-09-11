#include "MazeForgeEditor.h"

#include "MazeForgeCore.h"
#include "Mode/MazeEdModeSettingsDetails.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"

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

	UE_LOG(LogMazeForge, Log, TEXT("MazeForgeEditor started."));
}

void FMazeForgeEditorModule::ShutdownModule()
{
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
