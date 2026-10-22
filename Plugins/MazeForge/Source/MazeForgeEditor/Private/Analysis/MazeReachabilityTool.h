#pragma once

#include "CoreMinimal.h"

class UMazeEdModeSettings;

/**
 *  The panel's side of the reachability check: gathers the starts and the ladders from the
 *  target's spawn asset, runs MazeReachability, and keeps what the overlay draws on the settings.
 */
namespace MazeReachabilityTool
{
	/** Checks the target and fills the settings' overlay and status line. */
	void Check(UMazeEdModeSettings& Settings);

	/** Joins the unreachable pieces to the rest, one undo step, then checks again. */
	void Fix(UMazeEdModeSettings& Settings);
}
