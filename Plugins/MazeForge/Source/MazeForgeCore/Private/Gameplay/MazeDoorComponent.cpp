#include "Gameplay/MazeDoorComponent.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/MazeKeyringComponent.h"
#include "Gameplay/MazeProgressSubsystem.h"
#include "Materials/MaterialInterface.h"
#include "MazeForgeCore.h"
#include "Spawner/MazeObjectIdComponent.h"
#include "UObject/ConstructorHelpers.h"

UMazeDoorComponent::UMazeDoorComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 0.0f;

	// The fallback panel, referenced here so it is cooked with the game.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		DoorMesh = Cube.Object;
	}
}

FVector UMazeDoorComponent::ResolveSize(bool bWarn) const
{
	if (Size.X > 0.0 && Size.Y > 0.0 && Size.Z > 0.0)
	{
		return Size;
	}
	const AActor* Owner = GetOwner();
	const UMazeObjectIdComponent* Id = Owner ? Owner->FindComponentByClass<UMazeObjectIdComponent>() : nullptr;
	if (Id && Id->DoorSize.X > 0.0 && Id->DoorSize.Y > 0.0 && Id->DoorSize.Z > 0.0)
	{
		return Id->DoorSize;
	}
	if (bWarn)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Door %s: no size — place it with the Door tool and Apply Changes, or set Size. 50 x 100 x 250 cm for now."),
			*GetNameSafe(Owner));
	}
	return FVector(50.0, 100.0, 250.0);
}

void UMazeDoorComponent::OnRegister()
{
	Super::OnRegister();

	// The panel is there in the editor too, at its size: a door should be seen where it stands.
	if (GetWorld() && !IsTemplate())
	{
		Resolved = ResolveSize(false);
		BuildPanel();
	}
}

void UMazeDoorComponent::OnUnregister()
{
	// Made by this component and nobody else's: they go with it.
	if (Panel)
	{
		Panel->DestroyComponent();
		Panel = nullptr;
	}
	if (Zone)
	{
		Zone->DestroyComponent();
		Zone = nullptr;
	}
	Super::OnUnregister();
}

void UMazeDoorComponent::RefreshPreview()
{
	Resolved = ResolveSize(false);
	BuildPanel();
}

void UMazeDoorComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor* Owner = GetOwner();
	const UMazeObjectIdComponent* Id = Owner ? Owner->FindComponentByClass<UMazeObjectIdComponent>() : nullptr;
	if (Id && LockId.IsNone())
	{
		LockId = Id->LockId;
	}
	Resolved = ResolveSize(true);

	// Unlocked once — open to anyone for the rest of the session.
	bLocked = !LockId.IsNone();
	const UMazeProgressSubsystem* Progress = UMazeProgressSubsystem::Get(this);
	if (bLocked && Id && Progress && Progress->IsUsed(Id->PlacementId))
	{
		bLocked = false;
	}

	// A room shown again plays BeginPlay again: start shut and let the zone decide.
	OpenAmount = 0.0f;
	Direction = 0;
	EmptyTime = 0.0f;
	Refused.Reset();

	BuildPanel();
	BuildZone();
}

void UMazeDoorComponent::BuildPanel()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// The panel: the mesh's box stretched onto the door's, bottom on this component's origin.
	// Transient: built afresh on every load from the door's size, never saved into the level.
	if (!Panel)
	{
		Panel = NewObject<UStaticMeshComponent>(Owner, NAME_None,
			RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
		Panel->SetMobility(EComponentMobility::Movable);
		Panel->SetupAttachment(this);
		Panel->RegisterComponent();
	}
	Panel->SetStaticMesh(DoorMesh);
	Panel->SetVisibility(true);
	Panel->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	if (DoorMaterial && DoorMesh)
	{
		for (int32 Slot = 0; Slot < FMath::Max(1, DoorMesh->GetStaticMaterials().Num()); ++Slot)
		{
			Panel->SetMaterial(Slot, DoorMaterial);
		}
	}

	FVector Scale = FVector::OneVector;
	FVector MeshCenter = FVector::ZeroVector;
	if (DoorMesh)
	{
		const FBox MeshBox = DoorMesh->GetBoundingBox();
		const FVector MeshSize = MeshBox.GetSize();
		// A hair inside the doorway across and in depth, so no face lies on the rock's.
		const double Inset = 2.0 * SurfaceInset;
		const FVector Panelled(FMath::Max(1.0, Resolved.X - Inset), FMath::Max(1.0, Resolved.Y - Inset), Resolved.Z);
		Scale = FVector(
			MeshSize.X > KINDA_SMALL_NUMBER ? Panelled.X / MeshSize.X : 1.0,
			MeshSize.Y > KINDA_SMALL_NUMBER ? Panelled.Y / MeshSize.Y : 1.0,
			MeshSize.Z > KINDA_SMALL_NUMBER ? Panelled.Z / MeshSize.Z : 1.0);
		MeshCenter = MeshBox.GetCenter();
	}
	Panel->SetRelativeScale3D(Scale);
	PanelBase = FVector(0.0, 0.0, Resolved.Z * 0.5) - MeshCenter * Scale;
	Panel->SetRelativeLocation(PanelBase + FVector(0.0, 0.0, OpenAmount * Resolved.Z * OpenFraction));
}

void UMazeDoorComponent::BuildZone()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// The zone: the doorway and Trigger Distance either side of it, floor to top. Game only.
	if (!Zone)
	{
		Zone = NewObject<UBoxComponent>(Owner, NAME_None, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
		Zone->SetMobility(EComponentMobility::Movable);
		Zone->SetupAttachment(this);
		Zone->RegisterComponent();
	}
	Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Zone->SetCollisionObjectType(ECC_WorldDynamic);
	Zone->SetCollisionResponseToAllChannels(ECR_Ignore);
	Zone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Zone->SetGenerateOverlapEvents(true);
	Zone->SetCanEverAffectNavigation(false);
	Zone->SetHiddenInGame(true);
	Zone->SetBoxExtent(FVector(Resolved.X * 0.5 + TriggerDistance, FMath::Max(Resolved.Y * 0.5, 100.0), Resolved.Z * 0.5));
	Zone->SetRelativeLocation(FVector(0.0, 0.0, Resolved.Z * 0.5));
}

bool UMazeDoorComponent::Wants(APawn* Pawn)
{
	if (!Pawn || (bOnlyPlayers && !Pawn->IsPlayerControlled()))
	{
		return false;
	}
	if (!bLocked)
	{
		return true;
	}

	// Locked: the key opens it, once and for good.
	UMazeKeyringComponent* Keyring = UMazeKeyringComponent::Find(Pawn);
	if (Keyring && Keyring->HasKey(LockId))
	{
		if (bConsumeKey)
		{
			Keyring->RemoveKey(LockId);
		}
		bLocked = false;
		if (UMazeProgressSubsystem* Progress = UMazeProgressSubsystem::Get(this))
		{
			Progress->MarkUsed(UMazeObjectIdComponent::GetPlacementId(GetOwner()));
		}
		UE_LOG(LogMazeForge, Log, TEXT("Door %s: unlocked with '%s' — opens for anyone from now on."),
			*GetNameSafe(GetOwner()), *LockId.ToString());
		OnUnlocked.Broadcast(Pawn);
		return true;
	}

	if (!Refused.Contains(Pawn))
	{
		Refused.Add(Pawn);
		OnRefused.Broadcast(Pawn);
	}
	return false;
}

void UMazeDoorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!Panel || !Zone)
	{
		return;
	}

	// Who is in the zone, and does any of them open it.
	TArray<AActor*> Inside;
	Zone->GetOverlappingActors(Inside, APawn::StaticClass());

	bool bWanted = false;
	bool bAnyone = false;
	for (AActor* Actor : Inside)
	{
		APawn* Pawn = Cast<APawn>(Actor);
		bAnyone = bAnyone || Pawn != nullptr;
		bWanted = Wants(Pawn) || bWanted;
	}

	// Someone who was turned away and has left may be turned away again next time.
	Refused.RemoveAll([&Inside](const TWeakObjectPtr<APawn>& Weak)
	{
		return !Weak.IsValid() || !Inside.Contains(Weak.Get());
	});

	// It comes down only on an empty zone: never on someone standing in the doorway.
	EmptyTime = bAnyone ? 0.0f : EmptyTime + DeltaTime;
	const bool bShouldOpen = bWanted || (OpenAmount > 0.0f && bAnyone);
	const float Target = bShouldOpen ? 1.0f : (EmptyTime >= CloseDelay ? 0.0f : OpenAmount);

	const int32 NewDirection = Target > OpenAmount ? 1 : (Target < OpenAmount ? -1 : 0);
	if (NewDirection != Direction)
	{
		if (NewDirection > 0)
		{
			OnOpening.Broadcast();
		}
		else if (NewDirection < 0)
		{
			OnClosing.Broadcast();
		}
		Direction = NewDirection;
	}
	if (Direction == 0)
	{
		return;
	}

	const float Travel = FMath::Max(1.0f, static_cast<float>(Resolved.Z) * OpenFraction);
	const float Speed = (Direction > 0 ? OpenSpeed : CloseSpeed) / Travel;
	OpenAmount = FMath::FInterpConstantTo(OpenAmount, Target, DeltaTime, Speed);
	Panel->SetRelativeLocation(PanelBase + FVector(0.0, 0.0, OpenAmount * Travel));

	// All the way up it is inside the rock: hidden, so nothing of it can show through.
	const bool bShow = !(bHideWhenOpen && OpenAmount >= 1.0f && OpenFraction >= 1.0f);
	if (Panel->IsVisible() != bShow)
	{
		Panel->SetVisibility(bShow);
	}

	if (Direction < 0 && OpenAmount <= 0.0f)
	{
		Direction = 0;
		OnClosed.Broadcast();
	}
	else if (Direction > 0 && OpenAmount >= 1.0f)
	{
		Direction = 0;
	}
}
