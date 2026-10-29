#include "Gameplay/MazeRopeComponent.h"

#include "CollisionQueryParams.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/MazeRopeAnchorComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "MazeForgeCore.h"
#include "Spawner/MazeObjectIdComponent.h"
#include "UObject/ConstructorHelpers.h"

UMazeRopeComponent::UMazeRopeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// The fallback piece, referenced here so it is cooked with the game.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cylinder.Succeeded())
	{
		RopeMesh = Cylinder.Object;
		MeshAxis = ESplineMeshAxis::Z;
	}
}

void UMazeRopeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (Length <= 0.0f)
	{
		const AActor* Owner = GetOwner();
		if (const UMazeObjectIdComponent* Id = Owner ? Owner->FindComponentByClass<UMazeObjectIdComponent>() : nullptr)
		{
			Length = Id->RopeLength;
		}
	}
	if (Length <= 0.0f)
	{
		UE_LOG(LogMazeForge, Warning,
			TEXT("Rope %s: no length — place it with the Rope tool and Apply Changes, or set Length. 300 cm for now."),
			*GetNameSafe(GetOwner()));
		Length = 300.0f;
	}

	Build();
}

void UMazeRopeComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	// Nobody stays unable to stand on it after it is gone — or after its room comes back.
	for (const FDropped& Fallen : Dropped)
	{
		ACharacter* Character = Fallen.Character.Get();
		if (Character && Strip)
		{
			Character->GetCapsuleComponent()->IgnoreComponentWhenMoving(Strip.Get(), false);
		}
	}
	Dropped.Reset();
	Riders.Reset();
	Super::EndPlay(Reason);
}

void UMazeRopeComponent::Build()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// The strip that carries him: its top on the rope's line, Z 0.
	if (!Strip)
	{
		Strip = NewObject<UBoxComponent>(Owner, TEXT("RopeStrip"));
		Strip->SetMobility(EComponentMobility::Movable);
		Strip->SetupAttachment(this);
		Strip->RegisterComponent();
	}
	Strip->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	// It carries feet; it must not pull the camera in or block traces through the gap.
	Strip->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Strip->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Strip->SetCanEverAffectNavigation(false);
	Strip->CanCharacterStepUpOn = ECB_Yes;
	Strip->SetHiddenInGame(true);
	Strip->SetBoxExtent(FVector(Length * 0.5f, CollisionDepth * 0.5f, 5.0f));
	Strip->SetRelativeLocation(FVector(Length * 0.5f, 0.0f, -5.0f));

	// The string: a node every 20 cm or so, ends fixed.
	const int32 Nodes = FMath::Clamp(FMath::RoundToInt(Length / 20.0f), 8, 160) + 1;
	NodeStep = Length / (Nodes - 1);
	Wave.Init(0.0f, Nodes);
	WaveSpeedAt.Init(0.0f, Nodes);

	// The pieces: as many as fit, so the texture keeps its scale.
	for (USplineMeshComponent* Piece : Pieces)
	{
		if (Piece)
		{
			Piece->DestroyComponent();
		}
	}
	Pieces.Reset();

	if (!RopeMesh)
	{
		UE_LOG(LogMazeForge, Warning, TEXT("Rope %s: no Rope Mesh — nothing to draw."), *Owner->GetName());
		bAsleep = false;
		return;
	}

	const FBox MeshBox = RopeMesh->GetBoundingBox();
	const FVector Size = MeshBox.GetSize();
	const int32 Axis = static_cast<int32>(MeshAxis.GetValue());
	PieceLength = static_cast<float>(FMath::Max(1.0, Size[Axis]));
	const double Across = FMath::Max(Size[(Axis + 1) % 3], Size[(Axis + 2) % 3]);
	PieceScale = Across > KINDA_SMALL_NUMBER ? static_cast<float>(Thickness / Across) : 1.0f;

	// A material drawn on a spline mesh needs "Used with Spline Meshes" ticked, or the game
	// quietly draws the default grey instead — say which one, and where the tick is.
	TArray<UMaterialInterface*> Used;
	if (RopeMaterial)
	{
		Used.Add(RopeMaterial);
	}
	else
	{
		for (const FStaticMaterial& Slot : RopeMesh->GetStaticMaterials())
		{
			Used.Add(Slot.MaterialInterface);
		}
	}
	for (const UMaterialInterface* Material : Used)
	{
		const UMaterial* Base = Material ? Material->GetMaterial() : nullptr;
		if (Base && !Base->GetUsageByFlag(MATUSAGE_SplineMesh))
		{
			UE_LOG(LogMazeForge, Warning,
				TEXT("Rope %s: material %s (its base %s) does not have 'Used with Spline Meshes' ticked — the rope "
				     "is drawn with the default material. Open %s, Details → Usage → tick Used with Spline Meshes, save. "
				     "An engine material cannot be changed: duplicate it into the project first."),
				*Owner->GetName(), *Material->GetName(), *Base->GetPathName(), *Base->GetName());
		}
	}

	const int32 Count = bStretchSingleMesh ? 1 : FMath::Clamp(FMath::RoundToInt(Length / PieceLength), 1, 96);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Pieces.Add(MakePiece());
	}

	bAsleep = false;
	UpdatePieces();
	FindTies();
}

USplineMeshComponent* UMazeRopeComponent::MakePiece()
{
	USplineMeshComponent* Piece = NewObject<USplineMeshComponent>(GetOwner());
	Piece->SetMobility(EComponentMobility::Movable);
	Piece->SetupAttachment(this);
	Piece->SetStaticMesh(RopeMesh);
	Piece->SetForwardAxis(MeshAxis, false);
	Piece->SetStartScale(FVector2D(PieceScale, PieceScale), false);
	Piece->SetEndScale(FVector2D(PieceScale, PieceScale), false);
	Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Piece->SetCanEverAffectNavigation(false);
	if (RopeMaterial)
	{
		for (int32 Slot = 0; Slot < FMath::Max(1, RopeMesh->GetStaticMaterials().Num()); ++Slot)
		{
			Piece->SetMaterial(Slot, RopeMaterial);
		}
	}
	Piece->RegisterComponent();
	return Piece;
}

void UMazeRopeComponent::FindTies()
{
	const UMazeRopeAnchorSubsystem* Registry = UMazeRopeAnchorSubsystem::Get(this);
	if (!Registry || !RopeMesh || AnchorSearchRadius <= 0.0f)
	{
		return;
	}

	const FTransform& Frame = GetComponentTransform();
	bool bChanged = false;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (TieAnchor[Side].IsValid())
		{
			continue;
		}
		const FVector End = Frame.TransformPosition(FVector(Side == 0 ? 0.0f : Length, 0.0f, 0.0f));
		if (UMazeRopeAnchorComponent* Anchor = Registry->FindNear(End, AnchorSearchRadius))
		{
			TieAnchor[Side] = Anchor;
			bChanged = true;
		}
	}
	if (!bChanged)
	{
		return;
	}

	// Rebuilt whole: few pieces, and only when an anchor turns up.
	for (USplineMeshComponent* Piece : TiePieces)
	{
		if (Piece)
		{
			Piece->DestroyComponent();
		}
	}
	TiePieces.Reset();

	for (int32 Side = 0; Side < 2; ++Side)
	{
		const UMazeRopeAnchorComponent* Anchor = TieAnchor[Side].Get();
		if (!Anchor)
		{
			continue;
		}

		// From the edge of the gap to the knot, in the rope's own space; straight, pieces end to end.
		const FVector From(Side == 0 ? 0.0f : Length, 0.0f, 0.0f);
		const FVector To = Frame.InverseTransformPosition(Anchor->GetComponentLocation());
		const float TieLength = static_cast<float>(FVector::Dist(From, To));
		if (TieLength < 1.0f)
		{
			continue;
		}
		const int32 Count = bStretchSingleMesh ? 1 : FMath::Clamp(FMath::RoundToInt(TieLength / PieceLength), 1, 16);
		const FVector Step = (To - From) / Count;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			USplineMeshComponent* Piece = MakePiece();
			Piece->SetStartAndEnd(From + Step * Index, Step, From + Step * (Index + 1), Step, true);
			TiePieces.Add(Piece);
		}
	}
}

// ---------------------------------------------------------------- the string

float UMazeRopeComponent::StaticSag(float X) const
{
	const float L = FMath::Max(1.0f, Length);
	const float T = FMath::Clamp(X, 0.0f, L);

	// Hanging under its own weight: a shallow arch, deepest in the middle.
	float Sag = RestSag * 4.0f * T * (L - T) / (L * L);

	// Under the feet: a point load on a taut string — two straight runs meeting under him, deepest
	// when he is in the middle of the span.
	if (LoadAmount > KINDA_SMALL_NUMBER)
	{
		const float A = FMath::Clamp(LoadX, 1.0f, L - 1.0f);
		const float Peak = SagUnderFeet * LoadAmount * 4.0f * A * (L - A) / (L * L);
		Sag += T <= A ? Peak * T / A : Peak * (L - T) / (L - A);
	}
	return Sag;
}

float UMazeRopeComponent::WaveAt(float X) const
{
	if (Wave.Num() < 2 || NodeStep <= 0.0f)
	{
		return 0.0f;
	}
	const float F = FMath::Clamp(X / NodeStep, 0.0f, static_cast<float>(Wave.Num() - 1));
	const int32 I = FMath::Min(FMath::FloorToInt(F), Wave.Num() - 2);
	return FMath::Lerp(Wave[I], Wave[I + 1], F - I);
}

float UMazeRopeComponent::GetSagAt(float X) const
{
	return StaticSag(X) - WaveAt(X);
}

void UMazeRopeComponent::Kick(float X, float Speed)
{
	if (Wave.Num() < 3 || NodeStep <= 0.0f)
	{
		return;
	}
	// Down is negative; a smooth bump KickWidth wide around the foot, ends left fixed.
	const float Radius = FMath::Max(KickWidth * 0.5f, NodeStep);
	for (int32 I = 1; I < Wave.Num() - 1; ++I)
	{
		const float Distance = FMath::Abs(I * NodeStep - X);
		if (Distance < Radius)
		{
			WaveSpeedAt[I] -= Speed * 0.5f * (1.0f + FMath::Cos(PI * Distance / Radius));
		}
	}
	bAsleep = false;
}

void UMazeRopeComponent::Simulate(float DeltaTime)
{
	const int32 N = Wave.Num();
	if (N < 3 || NodeStep <= 0.0f)
	{
		return;
	}

	// Explicit steps small enough to stay stable: c * dt / dx under 0.9.
	const float Courant = WaveSpeed * DeltaTime / NodeStep;
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(Courant / 0.9f), 1, 64);
	const float Dt = DeltaTime / Steps;
	const float K = (WaveSpeed * WaveSpeed) / (NodeStep * NodeStep);

	for (int32 Step = 0; Step < Steps; ++Step)
	{
		for (int32 I = 1; I < N - 1; ++I)
		{
			const float Accel = K * (Wave[I - 1] - 2.0f * Wave[I] + Wave[I + 1]) - Damping * WaveSpeedAt[I];
			WaveSpeedAt[I] += Accel * Dt;
		}
		for (int32 I = 1; I < N - 1; ++I)
		{
			Wave[I] += WaveSpeedAt[I] * Dt;
		}
	}
}

// ---------------------------------------------------------------- riders

void UMazeRopeComponent::UpdateRiders(float DeltaTime)
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World || !Strip)
	{
		return;
	}

	for (FRider& Rider : Riders)
	{
		Rider.bSeen = false;
	}

	// Who stands on the strip: a slab over it, and their movement base is the strip.
	const FTransform& Frame = GetComponentTransform();
	const FVector Center = Frame.TransformPosition(FVector(Length * 0.5f, 0.0f, 40.0f));
	const FVector Extent(Length * 0.5f, CollisionDepth * 0.5f + 20.0f, 60.0f);

	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MazeRopeRiders), false, Owner);
	World->OverlapMultiByObjectType(Overlaps, Center, Frame.GetRotation(), FCollisionObjectQueryParams(ECC_Pawn),
		FCollisionShape::MakeBox(Extent), Params);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		ACharacter* Character = Cast<ACharacter>(Overlap.GetActor());
		if (!Character || Character->GetMovementBaseObject() != Strip.Get())
		{
			continue;
		}

		const float X = Frame.InverseTransformPosition(Character->GetActorLocation()).X;
		FRider* Rider = Riders.FindByPredicate([Character](const FRider& R) { return R.Character.Get() == Character; });
		if (!Rider)
		{
			Rider = &Riders.AddDefaulted_GetRef();
			Rider->Character = Character;
			Rider->LastX = X;
			Kick(X, BoardKick);
			OnBoarded.Broadcast(Character);
		}
		Rider->bSeen = true;

		// Steps shake it; standing still lets it give way.
		const float Moved = FMath::Abs(X - Rider->LastX);
		Rider->LastX = X;
		Rider->SinceStep += Moved;
		if (Rider->SinceStep >= StepDistance)
		{
			Rider->SinceStep = 0.0f;
			Kick(X, StepKick);
		}

		const FVector Velocity = Character->GetVelocity();
		const float Speed = FVector(Velocity.X, Velocity.Y, 0.0f).Size();
		Rider->StillTime = Speed < StopSpeed ? Rider->StillTime + DeltaTime : 0.0f;
	}

	// Gone — jumped or ran off: the rope springs back.
	for (int32 Index = Riders.Num() - 1; Index >= 0; --Index)
	{
		if (!Riders[Index].bSeen || !Riders[Index].Character.IsValid())
		{
			Kick(Riders[Index].LastX, -BoardKick * 0.5f);
			Riders.RemoveAtSwap(Index);
		}
	}

	// Stood still too long: through he goes.
	for (int32 Index = Riders.Num() - 1; Index >= 0; --Index)
	{
		if (FallDelay > 0.0f && Riders[Index].StillTime >= FallDelay)
		{
			ACharacter* Character = Riders[Index].Character.Get();
			Riders.RemoveAtSwap(Index);
			Drop(Character);
		}
	}

	// The dip follows the first rider.
	if (Riders.Num() > 0)
	{
		LoadTarget = 1.0f;
		const float Target = Riders[0].LastX;
		LoadX = LoadAmount > 0.05f ? FMath::FInterpTo(LoadX, Target, DeltaTime, 25.0f) : Target;
	}
	else
	{
		LoadTarget = 0.0f;
	}
	LoadAmount = FMath::FInterpConstantTo(LoadAmount, LoadTarget, DeltaTime, 8.0f);
}

void UMazeRopeComponent::Drop(ACharacter* Character)
{
	if (!Character || !Strip)
	{
		return;
	}

	UE_LOG(LogMazeForge, Log, TEXT("Rope %s: %s stood still and fell."), *GetNameSafe(GetOwner()), *Character->GetName());

	Character->GetCapsuleComponent()->IgnoreComponentWhenMoving(Strip.Get(), true);
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->SetMovementMode(MOVE_Falling);
	}
	Dropped.Add({ Character, 0.0f });
	Kick(GetComponentTransform().InverseTransformPosition(Character->GetActorLocation()).X, -BoardKick);
	OnFell.Broadcast(Character);
}

void UMazeRopeComponent::UpdateDropped(float DeltaTime)
{
	const FTransform& Frame = GetComponentTransform();
	for (int32 Index = Dropped.Num() - 1; Index >= 0; --Index)
	{
		FDropped& Fallen = Dropped[Index];
		Fallen.Time += DeltaTime;
		ACharacter* Character = Fallen.Character.Get();
		if (!Character)
		{
			Dropped.RemoveAtSwap(Index);
			continue;
		}

		// Clear of it — below, off its ends, or long gone: the rope holds him again next time.
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const FVector Local = Frame.InverseTransformPosition(Character->GetActorLocation());
		const bool bBelow = Local.Z + Capsule->GetScaledCapsuleHalfHeight() < -SagUnderFeet - 10.0f;
		const bool bOff = Local.X < -50.0f || Local.X > Length + 50.0f;
		if (bBelow || bOff || Fallen.Time > 3.0f)
		{
			if (Strip)
			{
				Character->GetCapsuleComponent()->IgnoreComponentWhenMoving(Strip.Get(), false);
			}
			Dropped.RemoveAtSwap(Index);
		}
	}
}

// ---------------------------------------------------------------- tick

void UMazeRopeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateRiders(DeltaTime);
	UpdateDropped(DeltaTime);

	// An anchor's room may load after the rope's: look again now and then until both are found.
	if (AnchorSearchRadius > 0.0f && (!TieAnchor[0].IsValid() || !TieAnchor[1].IsValid()))
	{
		TieSearchTimer += DeltaTime;
		if (TieSearchTimer >= 1.0f)
		{
			TieSearchTimer = 0.0f;
			FindTies();
		}
	}

	// Nothing on it and the shiver died down: nothing to redraw.
	if (Riders.Num() == 0 && LoadAmount <= 0.0f)
	{
		float Energy = 0.0f;
		for (int32 I = 0; I < Wave.Num(); ++I)
		{
			Energy = FMath::Max(Energy, FMath::Abs(Wave[I]) + FMath::Abs(WaveSpeedAt[I]) * 0.05f);
		}
		if (Energy < 0.02f)
		{
			if (!bAsleep)
			{
				for (int32 I = 0; I < Wave.Num(); ++I)
				{
					Wave[I] = 0.0f;
					WaveSpeedAt[I] = 0.0f;
				}
				UpdatePieces();
				if (Strip)
				{
					Strip->SetRelativeLocation(FVector(Length * 0.5f, 0.0f, -5.0f));
				}
				bAsleep = true;
			}
			return;
		}
	}
	bAsleep = false;

	Simulate(DeltaTime);

	// The strip dips with the rope under the feet, so he stands on the rope, not over it.
	if (Strip)
	{
		const float Under = Riders.Num() > 0 ? HeightAt(LoadX) : 0.0f;
		Strip->SetRelativeLocation(FVector(Length * 0.5f, 0.0f, Under - 5.0f));
	}

	UpdatePieces();
}

void UMazeRopeComponent::UpdatePieces()
{
	const int32 Count = Pieces.Num();
	if (Count == 0)
	{
		return;
	}

	const float Segment = Length / Count;
	const float H = FMath::Max(1.0f, NodeStep * 0.5f);
	auto Slope = [this, H](float X)
	{
		const float A = FMath::Clamp(X - H, 0.0f, Length);
		const float B = FMath::Clamp(X + H, 0.0f, Length);
		return B > A ? (HeightAt(B) - HeightAt(A)) / (B - A) : 0.0f;
	};

	for (int32 Index = 0; Index < Count; ++Index)
	{
		USplineMeshComponent* Piece = Pieces[Index];
		if (!Piece)
		{
			continue;
		}
		const float X0 = Segment * Index;
		const float X1 = Segment * (Index + 1);
		const FVector Start(X0, 0.0f, HeightAt(X0));
		const FVector End(X1, 0.0f, HeightAt(X1));
		const FVector StartTangent(Segment, 0.0f, Slope(X0) * Segment);
		const FVector EndTangent(Segment, 0.0f, Slope(X1) * Segment);
		Piece->SetStartAndEnd(Start, StartTangent, End, EndTangent, true);
	}
}
