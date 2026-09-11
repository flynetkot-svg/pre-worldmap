#include "MazeEdModeToolkit.h"

#include "DetailsViewArgs.h"
#include "IDetailsView.h"
#include "Mode/MazeEdMode.h"
#include "Mode/MazeEdModeSettings.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MazeForgeEditor"

void FMazeEdModeToolkit::Init(const TSharedPtr<IToolkitHost>& InitToolkitHost,
                              TWeakObjectPtr<UEdMode> InOwningMode)
{
	FPropertyEditorModule& PropertyEditorModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsArgs;
	DetailsArgs.bAllowSearch = false;
	DetailsArgs.bHideSelectionTip = true;
	DetailsArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	// The local variable must not be called DetailsView: that is the name of an FModeToolkit member.
	const TSharedRef<IDetailsView> SettingsView = PropertyEditorModule.CreateDetailView(DetailsArgs);

	if (UMazeEdMode* MazeMode = Cast<UMazeEdMode>(InOwningMode.Get()))
	{
		SettingsView->SetObject(MazeMode->GetSettings());
	}

	ToolkitWidget =
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(6.0f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(LOCTEXT("MazeHelp",
					"LMB — place a block\n"
					"Shift + LMB — erase\n"
					"Ctrl + drag — rectangle fill\n"
					"Ctrl + Shift + drag — rectangle erase\n"
					"PgUp / PgDn — active depth slice\n"
					"[ and ] — brush size\n"
					"Q — quick view back or front"))
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SettingsView
		];

	FModeToolkit::Init(InitToolkitHost, InOwningMode);
}

FName FMazeEdModeToolkit::GetToolkitFName() const
{
	return FName("MazeForgeEdMode");
}

FText FMazeEdModeToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("MazeToolkitName", "MazeForge");
}

#undef LOCTEXT_NAMESPACE
