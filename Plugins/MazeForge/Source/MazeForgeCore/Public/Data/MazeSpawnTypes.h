#pragma once

#include "CoreMinimal.h"
#include "Data/MazeTypes.h"
#include "MazeSpawnTypes.generated.h"

class AActor;

/**
 *  Where an object touches the mass of the maze.
 *
 *  A bitmask on the type ("a torch may sit on a wall or a ceiling") and a single value on the
 *  placement ("this torch is on a ceiling"). One kind, one contact edge of the footprint
 *  rectangle, and one validity rule for all of them: every cell of the rectangle is free, and
 *  the cells behind the contact edge are mass.
 */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EMazeAnchor : uint8
{
	None    = 0,
	/** The bottom edge rests on mass. Crates, enemies, anything that stands. */
	Floor   = 1 << 0,
	/** The top edge touches mass. Lamps, hanging signs. */
	Ceiling = 1 << 1,
	/** A side edge touches mass. Torches, pictures, levers. */
	Wall    = 1 << 2,
	/** Touches nothing. Floating decor, flying enemies, trigger volumes. */
	Free    = 1 << 3
};
ENUM_CLASS_FLAGS(EMazeAnchor);

/** Which anchor a placement actually used. Single value, unlike the type's mask. */
UENUM(BlueprintType)
enum class EMazeAnchorKind : uint8
{
	Floor   = 0,
	Ceiling = 1,
	Wall    = 2,
	Free    = 3
};

/**
 *  Whether this kind of object is part of a transition between mazes, and which end of it.
 *
 *  Two roles and no names. The world graph keys a transition by its placement id, which every
 *  placement already has, is unique for the life of the project and is never handed out twice —
 *  so a name would be a second identity to keep in step with the first, and the first is the one
 *  the runtime can actually read off a spawned actor.
 *
 *  It also means the whole game needs exactly two library types, not one per door.
 */
UENUM(BlueprintType)
enum class EMazeTransitionRole : uint8
{
	/** An ordinary object. Most things. */
	None  = 0,

	/**
	 *  Where the player arrives — the red square. A coordinate and not a thing, so the type
	 *  normally has no `Actor Class` and nothing is spawned for it.
	 *
	 *  Give it the `Free` anchor: an arrival point is usually a spot in mid-air, and every
	 *  other anchor would refuse it.
	 */
	Entry = 1,

	/**
	 *  Where the player leaves — the blue square. This one IS a thing: the door he walks into,
	 *  so the type needs an `Actor Class`.
	 *
	 *  That actor reads its own placement id off its `UMazeObjectIdComponent` and asks the world
	 *  graph where it leads. It never holds a destination itself, which is why one BP_Door
	 *  serves every door in the game.
	 */
	Gate  = 2
};

/**
 *  Doors and the keys to them.
 *
 *  Which door a key opens is not on the type but on the placement: its Lock Id, a colour or a
 *  channel such as Red. One red key opens every red door. So one door type and one key type
 *  serve every colour in the game.
 */
UENUM(BlueprintType)
enum class EMazeLockRole : uint8
{
	/** Neither. Most things. */
	None = 0,

	/** Closes the passage it stands in, floor to ceiling, until its key has been picked up. */
	Door = 1,

	/** Picked up on touch; opens every door with the same Lock Id. */
	Key = 2
};

/**
 *  A lift and the terminals that call it.
 *
 *  Which terminal calls which lift is worked out at export: a terminal standing at the level of a
 *  lift's stop belongs to the nearest such lift, and to that stop. Lift Id on the placements
 *  overrides that when it is not enough — a terminal far off, two lifts side by side.
 */
UENUM(BlueprintType)
enum class EMazeLiftRole : uint8
{
	/** Neither. Most things. */
	None = 0,

	/** The platform. Rides between its stops; the actor needs a Maze Lift component. */
	Platform = 1,

	/** A call terminal. Brings the platform to its stop; the actor needs a Maze Lift Call component. */
	Call = 2
};

/** How an object decides which way it faces. */
UENUM(BlueprintType)
enum class EMazeFacingMode : uint8
{
	/** Always the angle set on the type. */
	Fixed       = 0,
	/** Left or right along X, chosen by the seed. */
	RandomX     = 1,
	/**
	 *  Turned away from the mass it is attached to.
	 *
	 *  The reason this is a mode and not a checkbox: a torch on a wall has to face away from
	 *  that wall, and nobody wants to verify that by hand across a hundred torches.
	 */
	AwayFromWall = 2
};

/** What kind of content this is. Groups the brush palette and, later, the generator's rules. */
UENUM(BlueprintType)
enum class EMazeObjectCategory : uint8
{
	/** Ladders, ropes, climbable rock. */
	Climb  = 0,
	/** Crates, barrels, wardrobes, antennae — anything that is only looked at. */
	Decor  = 1,
	/** Ammo, weapons, quest items, traps. */
	Item   = 2,
	/** Enemies and their spawn points. */
	Enemy  = 3,
	/** Teleports, save points, triggers. */
	System = 4
};

/**
 *  One kind of object the spawner can place.
 *
 *  Lives in the shared object library, not in a maze: the same lamp is the same lamp in every
 *  maze, and duplicating its description per maze is the mistake we already avoided with the
 *  surface palette.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazeObjectType
{
	GENERATED_BODY()

	// ------------------------------------------------------------------ identity

	/**
	 *  What placements refer to. Stable, and deliberately NOT the index in the array.
	 *
	 *  Dragging a row in the editor would silently repoint every placement in the project at a
	 *  different object. That is the same class of quiet corruption as an index used for a cell
	 *  variant, and it is not worth saving a name lookup for.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FName TypeId;

	/** Shown in the brush palette. Free-form. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	EMazeObjectCategory Category = EMazeObjectCategory::Decor;

	/**
	 *  What gets spawned. Soft on purpose: a library of fifty types must not drag fifty
	 *  blueprints into memory just because the panel was opened.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	TSoftClassPtr<AActor> ActorClass;

	/** Colour of the marker in the 2D preview. Types must differ or the map reads as noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Identity")
	FLinearColor EditorColor = FLinearColor(0.9f, 0.7f, 0.2f);

	// ------------------------------------------------------------------ placement

	/**
	 *  Which anchors this object accepts. A mask, so a torch can declare wall or ceiling and let
	 *  the brush pick whichever fits where you clicked.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (Bitmask, BitmaskEnum = "/Script/MazeForgeCore.EMazeAnchor"))
	int32 AllowedAnchors = static_cast<int32>(EMazeAnchor::Floor);

	/**
	 *  Minimum clear space the object needs, in cells. X along the level, Y is height along Z.
	 *
	 *  In cells rather than read from the mesh, and that is deliberate. A blueprint that builds
	 *  its mesh at runtime has no bounds to read, and the check "does this crate fit in that
	 *  gap" is needed by the brush, the generator and the export alike. The cell is the unit
	 *  everything else here is measured in.
	 *
	 *  It is the space required, not the visual size. A ladder that stretches itself from floor
	 *  to opening declares the couple of cells it needs to stand in; how tall it draws itself is
	 *  the blueprint's business.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (ClampMin = "1"))
	FIntPoint FootprintCells = FIntPoint(1, 1);

	/**
	 *  Push the spawned actor until its own edge rests on the surface it is anchored to.
	 *
	 *  On by default, and it replaces a guess. The placement rules put the origin on the
	 *  contact surface and assumed the mesh was modelled to meet it there — a crate pivoted at
	 *  its base, a lamp at its top. Every prop modelled the other way went straight into the
	 *  mass: a lamp with a base pivot grew up into the ceiling, which is exactly what the
	 *  assumption promised and nothing in the editor showed.
	 *
	 *  Measured from the actor's own bounds at export, so it is right for any mesh and any
	 *  pivot without a number being typed anywhere.
	 *
	 *  Turn it off for a prop that is meant to cross the surface — a lamp on a chain, a pipe
	 *  sunk into a wall — and place it with Offset instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	bool bSnapToAnchorSurface = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	EMazeFacingMode FacingMode = EMazeFacingMode::Fixed;

	/** Used by Fixed, and as the starting point for the other modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FRotator FixedRotation = FRotator::ZeroRotator;

	/** Nudge inside the cell, in units. A lamp hanging a little below the ceiling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FVector Offset = FVector::ZeroVector;

	/**
	 *  The size the actor is spawned at.
	 *
	 *  It lives here because the export owns every object actor it makes: each Apply Changes
	 *  destroys them and builds them again from the type. A size typed into the level therefore
	 *  survives until the next Apply and no longer — set here, it comes back every time.
	 *
	 *  It scales the actor, not the space it claims: FootprintCells is declared, not measured,
	 *  so an object grown well past its footprint will happily overlap its neighbours. Grow the
	 *  footprint with it when the object is meant to keep them away.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement",
		meta = (AllowPreserveRatio))
	FVector Scale = FVector::OneVector;

	/**
	 *  Whether placements of this type are transition points, and which end.
	 *
	 *  A transition point is published into the maze's manifest under its placement id, and the
	 *  world graph joins one maze's Gate to another maze's Entry. See EMazeTransitionRole.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	EMazeTransitionRole TransitionRole = EMazeTransitionRole::None;

	/**
	 *  A door or a key. The reachability check reads it — a door closes its passage until a key
	 *  with the same Lock Id can be had — and Generate Locks places types with these roles. The
	 *  actor itself needs a Maze Lock or a Maze Key component to act on it in the game.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	EMazeLockRole LockRole = EMazeLockRole::None;

	/**
	 *  A lift platform or a lift call terminal. Platforms are stood with the Lift tool, two stops
	 *  each; terminals with the object brush, at the floor of a stop. See EMazeLiftRole.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	EMazeLiftRole LiftRole = EMazeLiftRole::None;

	// ------------------------------------------------------------------ rope

	/**
	 *  A rope stretched over a gap, run across and fallen off when stood still on. Stood with the
	 *  Rope tool, two clicks — one ledge, the other — at one level; the actor needs a Maze Rope
	 *  component. Footprint and anchors do not apply: the rope is as long as its gap.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope")
	bool bRope = false;

	/**
	 *  For a rope: what is stood on the ledge at each end — a hook, a post. An ordinary library
	 *  type, best with the Floor anchor. Empty — the rope alone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rope", meta = (EditCondition = "bRope"))
	FName RopeAnchorType;

	// ------------------------------------------------------------------ ladder

	/**
	 *  For a ladder: the platform stood at the top of the hole it climbs to.
	 *
	 *  A ladder is two pieces. This one — the ladder itself — stands on the lower floor; the
	 *  platform's pivot goes on the upper floor's level, at the top of the hole, above the
	 *  ladder. The Ladder tool, the Classic generator and Fix Reachability all put both down.
	 *  Give the platform type the Free anchor: there is a hole under it, not a floor.
	 *  Empty — a one-piece ladder.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ladder")
	FName TopPieceType;

	/**
	 *  A number property on the actor that gets the height of the climb — NumberOfRungs, say.
	 *
	 *  Measured at export, from the floor under the ladder to the upper floor's level at the top
	 *  of the hole, so a ladder that builds itself in its construction script is as tall as the
	 *  hole it stands in. Int or float. Empty — the actor is spawned as it is.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ladder")
	FName ClimbHeightProperty;

	/**
	 *  Centimetres per one unit of that property — the step between rungs. The height is divided
	 *  by it and rounded. 0 — the height goes in as centimetres.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ladder", meta = (ClampMin = "0", Units = "cm",
		EditCondition = "ClimbHeightProperty != NAME_None"))
	float ClimbUnitCm = 0.0f;

	/** Added after the division: minus the rungs the top section already has, plus a spare one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ladder",
		meta = (EditCondition = "ClimbHeightProperty != NAME_None"))
	int32 ClimbExtraUnits = 0;

	// ------------------------------------------------------------------ variants

	/**
	 *  Mix with the other types of the same Category when placed for you.
	 *
	 *  Wherever this type is named as THE type to place — a generator's Ladder Type, the Ladder
	 *  tool, Fix Reachability, Generate Locks, the start and exit markers — each placement may
	 *  get any library type of the same Category and the same role (door, key, transition, a
	 *  ladder's platform or not), picked at random but the same pick every time for the same
	 *  spot. Generate Objects mixes on its own already: a rule with Target = Category.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Variants")
	bool bRandomVariant = false;

	bool AllowsAnchor(EMazeAnchorKind Kind) const
	{
		return (AllowedAnchors & (1 << static_cast<int32>(Kind))) != 0;
	}
};

/**
 *  One placed object.
 *
 *  Deliberately small and deliberately in grid coordinates: the room is worked out at export
 *  from the cell, never stored. Re-slicing moves the room boundaries, and a stored room id
 *  would go stale the moment Room Size changed.
 */
USTRUCT(BlueprintType)
struct MAZEFORGECORE_API FMazePlacement
{
	GENERATED_BODY()

	/** Which type in the library. See FMazeObjectType::TypeId for why this is a name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FName TypeId;

	/** Bottom-left cell of the footprint rectangle. X along the level, Y along world Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FIntPoint CellXZ = FIntPoint::ZeroValue;

	/**
	 *  Depth as a BAND, not as a cell index, and this is the point of the field.
	 *
	 *  Depth profiles get edited — this project went from 9/4/9 to 3/2/2 mid-flight. A stored
	 *  cell index would have quietly moved every object the moment that happened, with nothing
	 *  to report it. "On the backdrop" stays "on the backdrop" whatever the profile becomes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	EMazeDepthBand Band = EMazeDepthBand::Play;

	/**
	 *  Which anchor was used when this was placed.
	 *
	 *  Stored rather than worked out again at export, and that buys a free check: the export
	 *  compares it against the geometry as it now stands and reports where the two have parted
	 *  company. Editing the maze under existing decor stops being a silent problem.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	EMazeAnchorKind Anchor = EMazeAnchorKind::Floor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FRotator Rotation = FRotator::ZeroRotator;

	/** On top of the type's own offset. This one is the hand adjustment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FVector Offset = FVector::ZeroVector;

	/**
	 *  Stable identity for the save system. Zero means "never exported".
	 *
	 *  Assigned at the first export and never reassigned: the numbers key a runtime registry of
	 *  what the player has done, and renumbering them would repoint every save at the wrong
	 *  object. Allocated from a counter on the asset rather than from the array index, so
	 *  deleting a placement leaves the rest alone and frees nothing for reuse.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Placement")
	int32 Id = 0;

	/**
	 *  Put here by the generator rather than by a hand.
	 *
	 *  The one thing that separates the two, and it has to be stored rather than worked out:
	 *  nothing about a crate's position says who decided to put it there. Regenerating clears
	 *  the placements that carry this and leaves every other one exactly where it is — which is
	 *  what makes a transition point, or a crate somebody deliberately put on a ledge, safe to
	 *  keep while the decor is thrown away and scattered again.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bGenerated = false;

	/**
	 *  Put here by the maze generator as part of the maze itself — the ladder in a shaft.
	 *
	 *  A third origin, because neither of the other two fits. Generate Objects must not throw it
	 *  away with the decor: without it the shaft cannot be climbed. And it is not the designer's
	 *  either: the next maze generation moves the shafts, and a ladder left where a shaft used
	 *  to be is a ladder into a wall. So Generate Objects keeps it, as it keeps anything placed
	 *  by hand, and the maze generator replaces every one of them on its next run.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bStructural = false;

	/**
	 *  For a door or a key: which keys open which doors. Any name — Red, Blue, Vault. One key
	 *  opens every door with its Lock Id. Ignored on anything else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FName LockId;

	/**
	 *  For a ladder: how many cells it climbs, from where it stands to the level of its top.
	 *  Set by the Ladder tool, Place All Ladders and the generators; the export sizes the ladder
	 *  by it and does not ask the anchor rules — the ladder was fitted to its shaft. 0 — an
	 *  ordinary object (a ladder placed with the object brush is measured at export).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0"))
	int32 ClimbCells = 0;

	/**
	 *  For a lift and its terminals: which terminals call which lift. Empty — worked out at
	 *  export: the lift is named after its cell, and a terminal goes to the nearest lift with a
	 *  stop at its level. A lift's Climb Cells is its travel, from the lower stop to the upper.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	FName LiftId;

	/** For a lift: it waits at the upper stop when the game starts, not the lower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement")
	bool bLiftStartsAtTop = false;

	/**
	 *  For a rope: how many cells of gap it spans, from its own cell to the right. Its cell row is
	 *  the one the player runs in; the rope lies along the bottom of it, level with the ledges.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Placement", meta = (ClampMin = "0"))
	int32 SpanCells = 0;

	/** Put here by Generate Locks. The next Generate Locks, or a new maze, replaces it. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Placement")
	bool bFromLockGenerator = false;

	/** The footprint rectangle in cells, given the type that owns it. Max exclusive. */
	FIntPoint MaxCellXZ(const FIntPoint& FootprintCells) const
	{
		return FIntPoint(CellXZ.X + FMath::Max(1, FootprintCells.X),
		                 CellXZ.Y + FMath::Max(1, FootprintCells.Y));
	}
};
