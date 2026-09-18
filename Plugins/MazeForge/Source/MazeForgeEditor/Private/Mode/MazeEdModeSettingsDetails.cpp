#include "MazeEdModeSettingsDetails.h"

#include "Assets/MazeBuildSettings.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeObjectLibrary.h"
#include "Assets/MazeSpawnAsset.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Mode/MazeEdModeSettings.h"
#include "Styling/AppStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MazeForgeEditor"

namespace
{
	/** Side of one swatch in Slate units. Big enough to hit, small enough to fit a dozen in a row. */
	constexpr float SwatchSize = 24.0f;

	/** A variant with no name of its own still has to be callable something in the tooltip. */
	FText VariantLabel(const FMazeSurfaceVariant& Variant, int32 Index)
	{
		return Variant.Name.IsNone()
			? FText::Format(LOCTEXT("UnnamedVariant", "Variant {0}"), FText::AsNumber(Index))
			: FText::FromName(Variant.Name);
	}

	/** The palette of the type currently being painted, or null if there is nothing to choose from. */
	const FMazeCellVisual* FindVisual(const UMazeEdModeSettings& Settings)
	{
		const UMazeGridAsset* Asset = Settings.TargetAsset.LoadSynchronous();
		const UMazeBuildSettings* Build = Asset ? Asset->BuildSettings.LoadSynchronous() : nullptr;

		return Build ? Build->FindVisual(Settings.PaintType) : nullptr;
	}
}

TSharedRef<IDetailCustomization> FMazeEdModeSettingsDetails::MakeInstance()
{
	return MakeShared<FMazeEdModeSettingsDetails>();
}

FMazeEdModeSettingsDetails::~FMazeEdModeSettingsDetails()
{
	// The settings object outlives this customization: the mode keeps it alive while the editor
	// runs, and the panel is rebuilt every time the mode is entered. A subscription left behind
	// would fire into a destroyed customization.
	if (SettingsChangedHandle.IsValid() && Settings.IsValid())
	{
		Settings->OnSettingsChanged.Remove(SettingsChangedHandle);
	}
}

void FMazeEdModeSettingsDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);

	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (UMazeEdModeSettings* Candidate = Cast<UMazeEdModeSettings>(Object.Get()))
		{
			Settings = Candidate;
			break;
		}
	}

	if (!Settings.IsValid())
	{
		return;
	}

	// The panel reads top to bottom the way the job goes: pick a target, generate or draw, look
	// at it, keep a state worth coming back to, build it. Anything below that line is either the
	// edit cycle for a maze that already exists, or a step of the pipeline reached for by hand.
	//
	// Without this the engine would order the sections alphabetically, because that is the order
	// the CallInEditor buttons create their categories in: Advanced, Brush, Build, Display,
	// Snapshot, Target. Every number below is a position, not a priority.
	IDetailCategoryBuilder& Target   = DetailBuilder.EditCategory("Target");
	IDetailCategoryBuilder& Generate = DetailBuilder.EditCategory("Generate");
	IDetailCategoryBuilder& Brush    = DetailBuilder.EditCategory("Brush");
	IDetailCategoryBuilder& ObjectsCat = DetailBuilder.EditCategory("Objects");
	IDetailCategoryBuilder& Display  = DetailBuilder.EditCategory("Display");
	IDetailCategoryBuilder& Snapshot = DetailBuilder.EditCategory("Snapshot");
	IDetailCategoryBuilder& Build    = DetailBuilder.EditCategory("Build");
	IDetailCategoryBuilder& Edit     = DetailBuilder.EditCategory("Edit");
	IDetailCategoryBuilder& World    = DetailBuilder.EditCategory("World");
	IDetailCategoryBuilder& Advanced = DetailBuilder.EditCategory("Advanced");

	Target.SetSortOrder(0);
	Generate.SetSortOrder(1);
	Brush.SetSortOrder(2);
	ObjectsCat.SetSortOrder(3);
	Display.SetSortOrder(4);
	Snapshot.SetSortOrder(5);
	Build.SetSortOrder(6);
	Edit.SetSortOrder(7);

	// Its own section, above Advanced rather than inside it. The world is the game as a whole
	// rather than the maze on screen, and the one button somebody needs right after cloning the
	// repository should not sit at the bottom of a section that is folded away by default.
	World.SetSortOrder(8);
	Advanced.SetSortOrder(9);

	// Folded away by default, and that is the whole point of it: the pipeline run one step at a
	// time is what you reach for when Apply Changes has gone wrong, not what you look at daily.
	Advanced.InitiallyCollapsed(true);

	BuildGenerateRow(Generate);
	BuildObjectPalette(ObjectsCat);
	BuildEditRows(Edit);

	Brush.AddCustomRow(LOCTEXT("SurfacesFilter", "surface variant palette"))
	.WholeRowContent()
	[
		SAssignNew(PaletteBox, SBox)
	];

	// The row follows the paint type, and the type is edited a couple of rows above it. The
	// settings delegate is the only thing that reports that change without polling.
	SettingsChangedHandle = Settings->OnSettingsChanged.AddSP(
		this, &FMazeEdModeSettingsDetails::OnSettingsChanged);

	RebuildPalette();
}

void FMazeEdModeSettingsDetails::BuildGenerateRow(IDetailCategoryBuilder& Category)
{
	const TWeakObjectPtr<UMazeEdModeSettings> WeakSettings = Settings;

	// Bound, not computed once: the generator is chosen in the asset, in another panel, and the
	// button has to notice that without anyone thinking to refresh this one.
	auto CanGenerate = [WeakSettings]()
	{
		const UMazeEdModeSettings* Live = WeakSettings.Get();
		return Live && !Live->IsGeneratorManual();
	};

	auto ModeText = [WeakSettings]()
	{
		const UMazeEdModeSettings* Live = WeakSettings.Get();
		if (!Live)
		{
			return FText::GetEmpty();
		}

		return Live->IsGeneratorManual()
			? LOCTEXT("ManualMode", "Current mode: manual drawing — there is nothing to generate.")
			: FText::Format(LOCTEXT("GeneratorMode", "Generator: {0}"), Live->GetGeneratorName());
	};

	Category.AddCustomRow(LOCTEXT("GenerateFilter", "generate maze"))
	.WholeRowContent()
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda(CanGenerate)
			.ToolTipText(LOCTEXT("GenerateTip",
				"Fills the grid using the generator set on the target asset. "
				"Whatever is drawn now is snapshotted first."))
			.OnClicked_Lambda([WeakSettings]()
			{
				if (UMazeEdModeSettings* Live = WeakSettings.Get())
				{
					Live->GenerateMaze();
					Live->OnSettingsChanged.Broadcast();
				}

				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(LOCTEXT("GenerateMaze", "Generate Maze"))
			]
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f, 6.0f, 2.0f, 2.0f)
		[
			// A disabled button with no explanation reads as a broken one. This line is the
			// difference between "it is off" and "it is off because of a choice you made".
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda(ModeText)
			.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.70f)))
		]
	];
}

void FMazeEdModeSettingsDetails::BuildObjectPalette(IDetailCategoryBuilder& Category)
{
	Category.AddCustomRow(LOCTEXT("ObjectsFilter", "object palette"))
	.WholeRowContent()
	[
		SAssignNew(ObjectBox, SBox)
	];

	RebuildObjectPalette();
}

void FMazeEdModeSettingsDetails::RebuildObjectPalette()
{
	if (!ObjectBox.IsValid() || !Settings.IsValid())
	{
		return;
	}

	const TWeakObjectPtr<UMazeEdModeSettings> WeakSettings = Settings;
	const UMazeObjectLibrary* Library = Settings->GetObjectLibrary();

	if (!Library || Library->Types.Num() == 0)
	{
		ObjectBox->SetContent(
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("NoLibrary",
				"No object library. Set Spawns on the maze asset, and a Library on that, then add "
				"types to it.")));
		return;
	}

	const TSharedRef<SWrapBox> Swatches = SNew(SWrapBox).UseAllottedSize(true);

	for (const FMazeObjectType& Type : Library->Types)
	{
		// A type with no id cannot be referred to by a placement, so it cannot be painted with.
		// It is skipped here and complained about by the library itself.
		if (Type.TypeId.IsNone())
		{
			continue;
		}

		const FName TypeId = Type.TypeId;
		const FLinearColor Color = Type.EditorColor;

		const FText Label = Type.DisplayName.IsEmpty()
			? FText::FromName(TypeId)
			: Type.DisplayName;

		auto FrameColor = [WeakSettings, TypeId]()
		{
			const UMazeEdModeSettings* Live = WeakSettings.Get();
			return (Live && Live->PaintObjectType == TypeId)
				? FSlateColor(FLinearColor::White)
				: FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		};

		Swatches->AddSlot()
		.Padding(2.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(0.0f)
			.ToolTipText(FText::Format(
				LOCTEXT("ObjectTooltip", "{0}  ({1} x {2} cells)"), Label,
				FText::AsNumber(FMath::Max(1, Type.FootprintCells.X)),
				FText::AsNumber(FMath::Max(1, Type.FootprintCells.Y))))
			.OnClicked_Lambda([WeakSettings, TypeId]()
			{
				if (UMazeEdModeSettings* Live = WeakSettings.Get())
				{
					Live->PaintObjectType = TypeId;

					// Picking an object is picking the object brush. Selecting a type and then
					// painting mass with it would be nobody's intention.
					Live->Tool = EMazeEditTool::Objects;
					Live->SaveConfig();
				}

				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor_Lambda(FrameColor)
				.Padding(2.0f)
				[
					SNew(SColorBlock)
					.Color(Color)
					.Size(FVector2D(SwatchSize, SwatchSize))
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
				]
			]
		];
	}

	auto SelectedText = [WeakSettings]()
	{
		const UMazeEdModeSettings* Live = WeakSettings.Get();
		if (!Live)
		{
			return FText::GetEmpty();
		}

		const UMazeObjectLibrary* LiveLibrary = Live->GetObjectLibrary();
		const FMazeObjectType* Selected = LiveLibrary
			? LiveLibrary->FindType(Live->PaintObjectType)
			: nullptr;

		if (!Selected)
		{
			return LOCTEXT("NoObjectSelected", "No object selected — click a swatch.");
		}

		const UMazeSpawnAsset* Spawns = Live->GetSpawnAsset();
		const int32 Placed = Spawns ? Spawns->Placements.Num() : 0;
		const int32 Unassigned = Spawns ? Spawns->CountUnassigned() : 0;

		// Both names, and not just the pretty one. The log speaks TypeId and the panel used to
		// speak DisplayName, so a type called "Box" on screen appeared in the log as 'Floor' and
		// an evening went into wondering which object the warnings were even about. They cost
		// four characters together; showing one of them costs an hour.
		const FText Name = Selected->DisplayName.IsEmpty()
			? FText::FromName(Selected->TypeId)
			: FText::Format(LOCTEXT("ObjectName", "{0} ({1})"),
				Selected->DisplayName, FText::FromName(Selected->TypeId));

		// The count of not-yet-exported placements is the honest reading of "drawn but not
		// created". Ids are handed out by the export, so this number is how much of the decor
		// has never been built.
		return FText::Format(
			LOCTEXT("ObjectSelected", "Placing: {0}   |   placed {1}, of them never exported {2}"),
			Name, FText::AsNumber(Placed), FText::AsNumber(Unassigned));
	};

	ObjectBox->SetContent(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			Swatches
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f, 6.0f, 2.0f, 2.0f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda(SelectedText)
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.35f)))
		]);
}

void FMazeEdModeSettingsDetails::BuildEditRows(IDetailCategoryBuilder& Category)
{
	const TWeakObjectPtr<UMazeEdModeSettings> WeakSettings = Settings;

	auto HasMaze = [WeakSettings]()
	{
		const UMazeEdModeSettings* Live = WeakSettings.Get();
		return Live && Live->HasBuiltMaze();
	};

	auto AddButton = [&Category, WeakSettings, HasMaze](
		const FText& Label, const FText& Tip, void (UMazeEdModeSettings::*Action)())
	{
		Category.AddCustomRow(Label)
		.WholeRowContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda(HasMaze)
			.ToolTipText(Tip)
			.OnClicked_Lambda([WeakSettings, Action]()
			{
				if (UMazeEdModeSettings* Live = WeakSettings.Get())
				{
					(Live->*Action)();
				}

				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(Label)
			]
		];
	};

	// Both are off until a slicing exists, and for the same reason: they take the room levels out
	// of the map and put them back, and a drawing that has never been built has no levels.
	AddButton(
		LOCTEXT("ChangeCurrentMaze", "Change Current Maze"),
		LOCTEXT("ChangeCurrentMazeTip",
			"Takes the room levels out of the map and flattens the grid, ready for the brush."),
		&UMazeEdModeSettings::ChangeCurrentMaze);

	AddButton(
		LOCTEXT("ApplyToCurrentMaze", "Apply Changes To Current Maze"),
		LOCTEXT("ApplyToCurrentMazeTip",
			"Closes the edit cycle: volume, rooms, meshes, levels, and the levels back into the map."),
		&UMazeEdModeSettings::ApplyChangesToCurrentMaze);

	Category.AddCustomRow(LOCTEXT("EditHintFilter", "edit cycle hint"))
	.WholeRowContent()
	[
		SNew(STextBlock)
		.AutoWrapText(true)
		.Text_Lambda([WeakSettings]()
		{
			const UMazeEdModeSettings* Live = WeakSettings.Get();
			return (Live && Live->HasBuiltMaze())
				? LOCTEXT("EditReady", "Change, edit with the brush, then Apply.")
				: LOCTEXT("EditNoMaze",
					"No maze yet — build one with Apply Changes first.");
		})
		.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.70f)))
	];
}

void FMazeEdModeSettingsDetails::OnSettingsChanged()
{
	// Both rows follow the target: the surface palette follows the paint type, the object
	// palette follows the library, and the library arrives with the target asset.
	RebuildPalette();
	RebuildObjectPalette();
}

void FMazeEdModeSettingsDetails::RebuildPalette()
{
	if (!PaletteBox.IsValid() || !Settings.IsValid())
	{
		return;
	}

	const UMazeEdModeSettings* Current = Settings.Get();
	const FMazeCellVisual* Visual = FindVisual(*Current);

	if (!Visual || Visual->Variants.Num() == 0)
	{
		PaletteBox->SetContent(
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(FText::Format(LOCTEXT("NoVariants",
				"{0} has a single surface. Add entries to Palette → Variants in Build Settings "
				"to get a choice here."),
				UEnum::GetDisplayValueAsText(Current->PaintType))));
		return;
	}

	// Everything the widgets capture is either a value or a weak pointer. A raw FMazeCellVisual*
	// points inside the palette's TMap, and editing that palette in this very panel would leave
	// every lambda here holding a dangling pointer.
	const TWeakObjectPtr<UMazeEdModeSettings> WeakSettings = Settings;

	const TSharedRef<SWrapBox> Swatches = SNew(SWrapBox).UseAllottedSize(true);

	for (int32 Index = 0; Index < Visual->Variants.Num(); ++Index)
	{
		const FMazeSurfaceVariant& Variant = Visual->Variants[Index];
		const FLinearColor Color = Variant.EditorColor;
		const FText Label = VariantLabel(Variant, Index);

		// The frame is what marks the selection, and its colour is bound rather than baked: a click
		// then only has to write the index, with no rebuild of the row while it handles the input.
		auto FrameColor = [WeakSettings, Index]()
		{
			const UMazeEdModeSettings* Live = WeakSettings.Get();
			return (Live && Live->PaintVariant == Index)
				? FSlateColor(FLinearColor::White)
				: FSlateColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.4f));
		};

		Swatches->AddSlot()
		.Padding(2.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.ContentPadding(0.0f)
			.ToolTipText(FText::Format(
				LOCTEXT("VariantTooltip", "{0}  (index {1})"), Label, FText::AsNumber(Index)))
			.OnClicked_Lambda([WeakSettings, Index]()
			{
				if (UMazeEdModeSettings* Live = WeakSettings.Get())
				{
					Live->PaintVariant = Index;

					// Written straight to the ini and deliberately without broadcasting: the
					// preview does not depend on the brush variant, and a broadcast here would
					// rebuild this very row from inside its own click handler.
					Live->SaveConfig();
				}

				return FReply::Handled();
			})
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BorderBackgroundColor_Lambda(FrameColor)
				.Padding(2.0f)
				[
					SNew(SColorBlock)
					.Color(Color)
					.Size(FVector2D(SwatchSize, SwatchSize))
					.AlphaDisplayMode(EColorBlockAlphaDisplayMode::Ignore)
				]
			]
		];
	}

	// The answer to "what am I about to paint", in words. A swatch says what the colour is, not
	// what the material behind it is called, and on a wall of a dozen greys that is the question.
	auto SelectedText = [WeakSettings]()
	{
		const UMazeEdModeSettings* Live = WeakSettings.Get();
		if (!Live)
		{
			return FText::GetEmpty();
		}

		// Resolved afresh on every paint instead of captured: the palette is edited in this same
		// panel, and a variant can be renamed or deleted while the row is on screen.
		const FMazeCellVisual* LiveVisual = FindVisual(*Live);

		if (LiveVisual && LiveVisual->Variants.IsValidIndex(Live->PaintVariant))
		{
			return FText::Format(LOCTEXT("SelectedVariant", "Painting: {0} — {1}"),
				UEnum::GetDisplayValueAsText(Live->PaintType),
				VariantLabel(LiveVisual->Variants[Live->PaintVariant], Live->PaintVariant));
		}

		return FText::Format(LOCTEXT("SelectedBase", "Painting: {0} — base surface"),
			UEnum::GetDisplayValueAsText(Live->PaintType));
	};

	PaletteBox->SetContent(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			Swatches
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2.0f, 6.0f, 2.0f, 2.0f)
		[
			// Tinted rather than styled by name: a colour needs no style lookup and cannot break
			// on an engine style being renamed.
			SNew(STextBlock)
			.Text_Lambda(SelectedText)
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.35f)))
		]);
}

#undef LOCTEXT_NAMESPACE
