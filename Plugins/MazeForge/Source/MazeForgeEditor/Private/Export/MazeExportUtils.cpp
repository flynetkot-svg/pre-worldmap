#include "Export/MazeExportUtils.h"

#include "Editor.h"
#include "Engine/World.h"
#include "MazeForgeCore.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace MazeExport
{
	const TCHAR* BandSuffix(EMazeDepthBand Band)
	{
		switch (Band)
		{
		case EMazeDepthBand::Background: return TEXT("BG");
		case EMazeDepthBand::Play:       return TEXT("Play");
		default:                         return TEXT("FG");
		}
	}

	FString ScopedRoot(const FString& Root, const FString& MazeName)
	{
		return MazeName.IsEmpty() ? Root : Root / MazeName;
	}

	FString MeshAssetName(const FString& MazeName, FName RoomId, EMazeDepthBand Band,
	                      bool bSplitByBand)
	{
		// The subfolder alone would keep the assets apart. The name carries the maze too, because
		// the Content Browser search, the Levels panel and the Outliner all show names without
		// their folders, and two identical rows there are worth nothing to anybody.
		const FString Prefix = MazeName.IsEmpty()
			? FString()
			: MazeName + TEXT("_");

		return bSplitByBand
			? FString::Printf(TEXT("SM_%s%s_%s"), *Prefix, *RoomId.ToString(), BandSuffix(Band))
			: FString::Printf(TEXT("SM_%s%s"), *Prefix, *RoomId.ToString());
	}

	FString CleanFolder(FString Folder)
	{
		Folder.TrimStartAndEndInline();
		while (Folder.RemoveFromStart(TEXT("/"))) {}
		while (Folder.RemoveFromEnd(TEXT("/"))) {}
		return Folder;
	}

	FString OutlinerRoot(const FString& RootSetting, const FString& MazeName)
	{
		const FString Root = CleanFolder(RootSetting);
		return MazeName.IsEmpty() ? Root : Root / MazeName;
	}

	FString LevelAssetName(const FString& MazeName, FName RoomId)
	{
		return MazeName.IsEmpty()
			? FString::Printf(TEXT("L_%s"), *RoomId.ToString())
			: FString::Printf(TEXT("L_%s_%s"), *MazeName, *RoomId.ToString());
	}

	FString MeshObjectPath(const FString& MeshRoot, const FString& AssetName)
	{
		return MeshRoot / AssetName + TEXT(".") + AssetName;
	}

	bool SavePackageToDisk(UPackage* Package, UObject* Asset, const FString& Extension)
	{
		if (!Package || !Asset)
		{
			return false;
		}

		const FString FileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), Extension);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_None;
		SaveArgs.bWarnOfLongFilename = false;
		// We already have our own progress dialog open; a nested one from SavePackage is not needed.
		SaveArgs.bSlowTask = false;
		SaveArgs.Error = GError;

		// This overload returns a bool, not an FSavePackageResultStruct.
		if (!UPackage::SavePackage(Package, Asset, *FileName, SaveArgs))
		{
			return false;
		}

		// CreatePackage marks the package as PKG_NewlyCreated, that is "not on disk yet".
		// A regular save clears the flag by itself, a manual one does not always, and the editor
		// then treats an asset that has already been written as unsaved.
		Package->ClearPackageFlags(PKG_NewlyCreated);
		Package->SetDirtyFlag(false);
		return true;
	}

	void FlushBatch(TArray<UPackage*>& InOutPackages, UPackage* KeepPackage,
	                int32& InOutFlushedCount)
	{
		if (InOutPackages.Num() == 0)
		{
			return;
		}

		// A package the caller still needs (the manifest) must not be unloaded.
		if (KeepPackage)
		{
			InOutPackages.Remove(KeepPackage);
		}

		// The world the designer has open is never touched, under any circumstances.
		if (GEditor)
		{
			if (const UWorld* EditorWorld = GEditor->GetEditorWorldContext().World())
			{
				InOutPackages.Remove(EditorWorld->GetPackage());
			}
		}

		if (InOutPackages.Num() == 0)
		{
			return;
		}

		// Render commands may still be holding the mesh buffers — we wait for them.
		FlushRenderingCommands();

		FText Error;
		if (!UPackageTools::UnloadPackages(InOutPackages, Error, /*bUnloadDirtyPackages*/ false))
		{
			// Not a disaster: the package may still be in use by the editor. We simply carry on,
			// and at worst we fall back to the previous behaviour.
			UE_LOG(LogMazeForge, Verbose, TEXT("Bake: could not unload the batch — %s"),
				*Error.ToString());
		}
		else
		{
			InOutFlushedCount += InOutPackages.Num();
		}

		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		InOutPackages.Reset();
	}
}
