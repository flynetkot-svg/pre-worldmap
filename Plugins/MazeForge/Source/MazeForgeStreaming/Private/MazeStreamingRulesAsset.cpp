#include "MazeStreamingRulesAsset.h"

#include "Rules/MazeRule_CameraFrame.h"
#include "Rules/MazeRule_PortalGraph.h"
#include "Rules/MazeRule_Radius.h"
#include "Rules/MazeRule_VelocityPredict.h"
#include "Rules/MazeRule_VerticalMotion.h"

UMazeStreamingRulesAsset::UMazeStreamingRulesAsset()
{
	// The default working set: the camera frame as the basis, the portal graph as the
	// main correction for the maze geometry, the rest are motion prediction.
	Rules.Add(CreateDefaultSubobject<UMazeRule_CameraFrame>(TEXT("CameraFrame")));
	Rules.Add(CreateDefaultSubobject<UMazeRule_PortalGraph>(TEXT("PortalGraph")));
	Rules.Add(CreateDefaultSubobject<UMazeRule_VelocityPredict>(TEXT("VelocityPredict")));
	Rules.Add(CreateDefaultSubobject<UMazeRule_VerticalMotion>(TEXT("VerticalMotion")));
	Rules.Add(CreateDefaultSubobject<UMazeRule_Radius>(TEXT("PlayerRadius")));
}
