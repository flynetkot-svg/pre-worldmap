#include "Export/MazeExportUtils.h"

#include "Assets/MazeBuildSettings.h"
#include "Assets/MazeGridAsset.h"
#include "Assets/MazeWorldManifest.h"
#include "Misc/MessageDialog.h"
#include "Editor.h"
#include "Engine/World.h"
#include "MazeForgeCore.h"
#include "Misc/PackageName.h"
#include "PackageTools.h"
#include "RenderingThread.h"
#include "UObject/ObjectRedirector.h"
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

	FString ManifestAssetName(const UMazeBuildSettings* Settings, const FString& MazeName)
	{
		FString Name = Settings ? Settings->ManifestAssetName : FString();
		Name.TrimStartAndEndInline();
		if (Name.IsEmpty())
		{
			Name = TEXT("DA_MazeWorldManifest");
		}

		// The manifest is the one asset a maze has exactly one of, so it collides even when the
		// rooms do not. It lives in the compartment as well, and says which maze it is in its
		// own name.
		if (!MazeName.IsEmpty())
		{
			Name += TEXT("_") + MazeName;
		}

		return Name;
	}

	FString ManifestPackageName(const UMazeBuildSettings* Settings, const FString& MazeName)
	{
		// An empty ManifestPackageRoot means "next to the levels": most projects have a single
		// maze, and a separate folder for one asset only gets in the way.
		FString Root = Settings ? Settings->ManifestPackageRoot : FString();
		Root.TrimStartAndEndInline();
		while (Root.RemoveFromEnd(TEXT("/"))) {}

		if (Root.IsEmpty())
		{
			Root = ScopedRoot(
				Settings ? Settings->LevelPackageRoot : FString(TEXT("/Game/MazeForge/Maps")),
				MazeName);
		}

		return Root / ManifestAssetName(Settings, MazeName);
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
		if (const UWorld* Shown = EditorWorld())
		{
			InOutPackages.Remove(Shown->GetPackage());
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

UWorld* MazeExport::EditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

bool MazeExport::ClearNameFor(UPackage* Package, const FString& ObjectName, const UClass* ExpectedClass)
{
	UObject* Existing = Package ? FindObject<UObject>(Package, *ObjectName) : nullptr;
	if (!Existing || (ExpectedClass && Existing->IsA(ExpectedClass)))
	{
		return true;
	}

	if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Existing))
	{
		UE_LOG(LogMazeForge, Log,
			TEXT("%s.%s was a redirector to %s, left by an earlier move. The rebuilt asset takes its place."),
			*Package->GetName(), *ObjectName,
			Redirector->DestinationObject ? *Redirector->DestinationObject->GetPathName() : TEXT("nothing"));

		Redirector->ClearFlags(RF_Public | RF_Standalone);
		Redirector->Rename(nullptr, GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty);
		Redirector->MarkAsGarbage();
		return true;
	}

	UE_LOG(LogMazeForge, Error,
		TEXT("%s.%s is a %s, not a %s. Not overwritten — move or rename that asset, then build again."),
		*Package->GetName(), *ObjectName, *Existing->GetClass()->GetName(),
		ExpectedClass ? *ExpectedClass->GetName() : TEXT("map"));
	return false;
}

bool MazeExport::EnsureOutputIsOurs(const UMazeGridAsset* Asset, const TCHAR* Action)
{
	if (!Asset)
	{
		return false;
	}

	const UMazeBuildSettings* Settings = Asset->BuildSettings.LoadSynchronous();
	const FString MazeName = Asset->GetSafeMazeName();
	const FString ManifestPackage = ManifestPackageName(Settings, MazeName);

	if (!FPackageName::DoesPackageExist(ManifestPackage))
	{
		return true; // never built under this name
	}

	const FString ManifestPath = ManifestPackage + TEXT(".") + ManifestAssetName(Settings, MazeName);
	const UMazeWorldManifest* Manifest = LoadObject<UMazeWorldManifest>(nullptr, *ManifestPath,
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Manifest || Manifest->BuiltFrom.IsEmpty())
	{
		return true; // written before owners were recorded; the next export records this one
	}

	// Loaded rather than compared as text: a grid asset that was renamed or moved leaves a
	// redirector behind, and following it lands on the very same asset — which is ours.
	const UMazeGridAsset* Owner = Cast<UMazeGridAsset>(FSoftObjectPath(Manifest->BuiltFrom).TryLoad());
	if (!Owner || Owner == Asset || Owner->GetSafeMazeName() != MazeName)
	{
		return true; // gone, ours, or renamed to another maze since
	}

	UE_LOG(LogMazeForge, Error,
		TEXT("%s refused: Maze Name '%s' is already used by %s. Building %s would overwrite its "
		     "levels, meshes and manifest. Give this maze a Maze Name of its own."),
		Action, *MazeName, *Owner->GetPathName(), *Asset->GetName());

	FMessageDialog::Open(EAppMsgType::Ok, FText::Format(
		NSLOCTEXT("MazeForgeEditor", "MazeNameTaken",
			"{0} stopped before writing anything.\n\n"
			"Maze Name '{1}' is already used by\n{2}\n\n"
			"Both mazes would write into the same levels, meshes and manifest, and this build "
			"would overwrite the other maze.\n\n"
			"Give {3} a Maze Name of its own (Build → Maze Name), then build again."),
		FText::FromString(Action), FText::FromString(MazeName.IsEmpty() ? TEXT("(empty)") : MazeName),
		FText::FromString(Owner->GetPathName()), FText::FromString(Asset->GetName())));

	return false;
}
