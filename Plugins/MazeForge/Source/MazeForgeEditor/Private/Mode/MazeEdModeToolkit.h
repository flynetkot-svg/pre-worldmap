#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"

/**
 *  Mode panel: a Details view of the brush settings plus a short controls cheat sheet.
 *
 *  The surface swatch row is not here. It belongs to the Brush section of the Details view and is
 *  added there by FMazeEdModeSettingsDetails: picking a surface is part of setting up the brush,
 *  and a separate block above the panel read as a different tool.
 */
class FMazeEdModeToolkit : public FModeToolkit
{
public:
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost,
	                  TWeakObjectPtr<UEdMode> InOwningMode) override;

	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	// ToolkitWidget and DetailsView are already members of FModeToolkit, so we do not declare our
	// own: a local ToolkitWidget would shadow the base one and the mode panel would stay empty.
	virtual TSharedPtr<SWidget> GetInlineContent() const override { return ToolkitWidget; }
};
