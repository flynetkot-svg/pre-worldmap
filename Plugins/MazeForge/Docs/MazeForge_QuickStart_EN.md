# MazeForge — Quick Start

**What you get:** a world of two mazes with a working transition between them.
**Version:** MazeForge 0.4.0 · Unreal Engine 5.8.2
**Field-by-field reference:** `MazeForge_Reference_EN.md`
**По-русски:** `MazeForge_QuickStart_RU.md`

This document is one straight path with no branches. Do it in order and skip nothing. Every stage ends with a **check** — a short sign that the step worked. If a check does not match, stop there: it only gets worse further on.

Anything not mentioned here, leave alone. The defaults are chosen for a first maze.

---

## Contents

1. [Three things to understand first](#1-three-things-to-understand-first)
2. [Installation](#2-installation)
3. [The map of assets](#3-the-map-of-assets)
4. [Maze 1: draw it and build it](#4-maze-1-draw-it-and-build-it)
5. [The character: hooking up streaming](#5-the-character-hooking-up-streaming)
6. [Checking it in game](#6-checking-it-in-game)
7. [Objects: the library and the brush](#7-objects-the-library-and-the-brush)
8. [Objects: the generator and its rules](#8-objects-the-generator-and-its-rules)
9. [Maze 2](#9-maze-2)
10. [The transition between mazes](#10-the-transition-between-mazes)
11. [The world graph and the world map](#11-the-world-graph-and-the-world-map)
12. [Checking the transition](#12-checking-the-transition)
13. [When it does not work](#13-when-it-does-not-work)
14. [Cheat sheet](#14-cheat-sheet)

---

## 1. Three things to understand first

Without these the rest will look strange.

**The axes.** X runs along the level. Z is height. **Y is depth**, the camera axis. The camera sits at +Y and looks inwards, towards −Y. Draw in the **Left** or **Right** view, or in perspective. The Top view shows you nothing meaningful.

**The depth bands.** Depth is divided into three bands:

| Band | What lives there | Collision |
|---|---|---|
| `Background` | decor behind the player | no |
| `Play` | the plane the player moves in | **yes** |
| `Foreground` | decor in front of the player | no |

World zero on Y is the centre of `Play`. Remember this: **the player exists only in `Play`.** An object placed in `Background` is something he can neither see properly nor touch. A transition point in `Background` will never work — the single most common mistake when building a first world.

**Plane and volume.** You always draw in **one plane**, one cell per column. The volume is grown in a separate step once the maze suits you. This is not a limitation; it is what lets you edit a map of hundreds of thousands of cells without the editor slowing down.

---

## 2. Installation

1. Copy the `MazeForge` folder into `<Project>/Plugins/`.
2. Right-click the `.uproject` → **Generate Visual Studio project files**.
3. Build the project.
4. Start the editor. **Edit → Plugins**, category **Level Design** — MazeForge should be enabled.

The plugin contains C++ and has to be compiled. Copying it into a project with no code will not work.

### Engine settings

Add to `Config/DefaultEngine.ini`:

```ini
[SystemSettings]
s.ForceGCAfterLevelStreamedOut=0
s.ContinuouslyIncrementalGCWhileLevelsPendingPurge=0
gc.TimeBetweenPurgingPendingKillObjects=120
s.AdaptiveAddToWorld.Enabled=1
```

The first two lines are **required**. Without them the engine runs a full blocking garbage collection every time a room is streamed out, and the character stutters as you run. That is the engine's default behaviour, not something the plugin does.

The third follows from the first two: rubbish now accumulates for longer. If `Mem` in `stat unit` climbs during a long run and never comes back down, lower it to 60.

**Check.** Right-click empty space in the Content Browser — the menu has a **MazeForge** section.

---

## 3. The map of assets

Seven kinds of asset. Do not be put off: five of them are created **once per project** and then left alone.

```
        ┌─────────────────────┐
        │ DA_MazeBuildSettings│  one per project: palette and build rules
        └──────────▲──────────┘
                   │ Build Settings
        ┌──────────┴──────────┐
        │   U_Maze (Maze Grid)│  ONE PER MAZE: grid, generator, slicing
        └──────────┬──────────┘
                   │ Spawns
        ┌──────────▼──────────┐
        │  DA_MazeSpawn_<name>│  ONE PER MAZE: where its objects stand
        └──────────┬──────────┘
                   │ Library
        ┌──────────▼──────────┐
        │ DA_MazeObjectLibrary│  one per project: what objects exist at all
        └─────────────────────┘

        ┌─────────────────────┐
        │  DA_MazeSpawnRules  │  one per project: how much of what to scatter
        └─────────────────────┘
        ┌─────────────────────┐
        │ DA_MazeStreamRules  │  one per project: streaming budgets
        └─────────────────────┘
        ┌─────────────────────┐
        │     DA_MazeWorld    │  one per project: how the mazes join up
        └─────────────────────┘

        ┌─────────────────────┐
        │ DA_MazeWorldManifest│  BUILD OUTPUT, never created by hand
        └─────────────────────┘
```

**Do not create the manifest.** The build writes it, one per maze, next to that maze's levels. It is the boundary between the editor and the game: the runtime sees only the manifest and knows nothing about the voxel grid.

Create them bottom-up along the arrows. This document walks you through it.

---

## 4. Maze 1: draw it and build it

### 4.1 Two assets you create once

Both are shared across the project and neither needs filling in — only creating. Do it now so they are already to hand later.

**Build settings.** Content Browser → right-click → **Miscellaneous → Data Asset → Maze Build Settings.** Call it `DA_MazeBuildSettings`.

**Change nothing inside.** Every default works: levels land in `/Game/MazeForge/Maps`, meshes in `/Game/MazeForge/Meshes`, and the palette is filled in by the constructor. The one thing worth doing later is putting your own materials into `Palette`. You do not need it for a first maze: with no materials the geometry comes out grey, but correct.

**Streaming rules.** Content Browser → right-click → **Miscellaneous → Data Asset → Maze Streaming Rules Asset.** Call it `DA_MazeStreamRules`.

Change nothing inside this one either: both the rule set and the budgets are created by the constructor. You will need it in §5, when you hook up the character — but it is easier to make it now, alongside its neighbour.

### 4.2 The maze itself

**Content Browser → right-click → MazeForge → Maze Grid.**

Call it `U_Maze_A`. Open it and fill in:

| Field | Value | Why |
|---|---|---|
| `Grid → Cell Size` | `(100, 100, 100)` | the standard UE cube |
| `Grid → Size XZ` | `(256, 128)` | plenty for a first try |
| `Grid → Depth` | `3 / 2 / 2` | Background / Play / Foreground |
| `Grid → World Origin` | `(0, 0, 0)` | where the maze sits in the world, see §9 |
| `Build → Maze Name` | **`A`** | see below — this matters |
| `Build → Build Settings` | `DA_MazeBuildSettings` | **required** |
| `Generation → Generator` | `Random (Saboteur-style complex)` | or leave whatever it was created with |
| `Slicing → Slicer` | `Uniform Grid`, `Room Size XZ = (32, 32)` | as is |

Leave the rest alone.

> **Fill in `Maze Name` right away.** An empty name is the "one maze in the project" behaviour. Room ids are not unique between mazes: every slicing starts at `R_000_000`. Two mazes with empty names write the same level names, the same mesh names and **one manifest between them** — building the second silently overwrites the first. You are making a world of two mazes, so both need a name. Anything that is not a letter, a digit or an underscore is stripped: the name becomes part of a package path.

There are deliberately no buttons inside the asset: it holds parameters, and the work happens in the mode panel.

### 4.3 The editing mode

Open any map (or make an empty one — it becomes your persistent level), then pick the **MazeForge** mode in the editor toolbar.

The panel is divided into sections, top to bottom:

| Section | For |
|---|---|
| `Target` | which maze is being edited |
| `Generate` | the generate button |
| `Brush` | the brush: cells or objects |
| `Objects` | the object palette and the placement generator |
| `Display` | what to show in the viewport |
| `Snapshot` | a snapshot of the drawing |
| `Build` | building |
| `Edit` | editing something already built |
| `World` | the world map |
| `Advanced` | individual pipeline steps, collapsed |

In `Target → Target Asset`, pick `U_Maze_A`.

**Check.** The `Status` line below it stops saying `no target set` and shows something like `empty · cells 0 · rooms 0 · meshes: no rooms · snapshot: none`.

### 4.4 Draw

Switch to the **Left** or **Right** view — the view menu is in the top-left corner of the viewport.

Two ways, take either:

**Generate it.** The `Generate` section → **Generate Maze**. The grid fills according to the generator set on the asset. The button is disabled while the generator is `Manual` — the caption under it says so.

**Draw it.** The `Brush` section, `Tool = Cells`, `Paint Type = Solid`. LMB paints, Ctrl + drag fills a rectangle. Holding `Q` temporarily lifts the dimming of the inactive layers.

Generating is enough for a first pass.

**Check.** `Status` now reads `plane · cells <a lot>`.

### 4.5 Build

The `Build` section → **Apply Changes**.

One button does six steps in a row:

1. snapshot of the plane (before the volume is grown);
2. grow the volume according to the depth profile;
3. slice into rooms;
4. bake the room meshes;
5. export the rooms into separate levels and write the manifest;
6. attach those levels to the open map.

**Check.** In the Output Log:

```
LogMazeForge: Mesh bake: rooms N ..., triangles ..., in 0.9 s.
LogMazeForge: Export: rooms N, levels N, meshes ...
LogMazeForge: Levels in /Game/MazeForge/Maps/A, manifest /Game/MazeForge/Maps/A/DA_MazeWorldManifest_A
LogMazeForge: Apply changes: N rooms, N rebuilt, 0 left alone, N levels attached; in 1.6 s. Save everything — Ctrl+Shift+S.
```

And the viewport now shows real geometry instead of preview cubes.

**Save everything: Ctrl+Shift+S.** The export writes its assets to disk itself, but attaching the levels changes the open map, and that has to be saved by hand.

> The log will carry lines reading `LogSpawn: Warning: UWorld::DestroyActor: World has no context!`, one per rebuilt actor. That is a false positive from the engine about attached sublevels. Nothing is broken; ignore them.

### 4.6 Move the Player Start

The project template put `Player Start` wherever suited it — and you have just built over that spot. The character will end up inside the mass and either stick fast or fall through.

In the **Left** or **Right** view, find an empty corridor and put `Player Start` in it. Then select it and check one number in Details:

| Field | Value |
|---|---|
| `Transform → Location → Y` | **`0`** |

Zero on Y is the centre of the `Play` band, the only one the player exists in. Looking at the viewport will not tell you: from the side all three bands overlap exactly, and a `Player Start` that has drifted into the background looks like it is standing in the corridor. The number in Details is the only way to see it.

Set X and Z by eye: an empty cell, preferably with a floor under it.

**Check.** Start PIE. The character stands in a corridor and walks, rather than falling or stuck in a wall. If he falls through the floor you put him in `Background` or `Foreground`: there is collision only in `Play`.

---

## 5. The character: hooking up streaming

Rooms are streamed around an **observer** — an actor carrying the component. Normally the player character.

1. Open the character blueprint.
2. **Add Component → Maze Streaming**.
3. Select the component and fill in, in Details:

| Field | Value |
|---|---|
| `Manifest` | `/Game/MazeForge/Maps/A/DA_MazeWorldManifest_A` |
| `Rules` | `DA_MazeStreamRules` — the one you made in §4.1 |
| `World Graph` | leave empty for now; §11 fills it in |

Leave the other fields alone. Their defaults work: the camera widens to the size of the maze, the screen fades on a transition, and the timings are set.

> **The plugin's main trap.** The maze asset has a field `Build → Built Manifest (output)`. It is **greyed out and read-only**, and the game does **not** read it. The runtime reads `Manifest` here, on the component, and that field is filled in by hand once. The two never follow each other. Build a maze under a new `Maze Name` and its manifest becomes a different asset — carrying it over here is still a manual step.

**Check.** The component appears in the Components list and `Manifest` is not empty.

---

## 6. Checking it in game

Start PIE.

**Check.** In the Output Log, right after start:

```
LogMazeForge: Streaming: BP_YourCharacter_C_0 reads manifest /Game/MazeForge/Maps/A/DA_MazeWorldManifest_A
LogMazeForge: Streaming: now reading manifest ... (was none).
LogMazeForge: Camera bounds set from the manifest: X 0..25600
```

Run around — rooms load and unload in silence. To watch it happen, type `MazeForge.DebugStreaming 1` in the console: the screen shows every room and how much the pool wants it loaded.

If you get an `Error` instead, see §13 — all three cases are covered there.

---

## 7. Objects: the library and the brush

### 7.1 The library

**Miscellaneous → Data Asset → Maze Object Library.** Call it `DA_MazeObjectLibrary`. One per project.

Add one entry to `Types` to try it:

| Field | Value |
|---|---|
| `Type Id` | `Crate` |
| `Display Name` | `Crate` |
| `Category` | `Decor` |
| `Actor Class` | your crate blueprint |
| `Editor Color` | anything distinguishable |
| `Allowed Anchors` | `Floor` |
| `Footprint Cells` | `(1, 1)` |

Everything else stays at its default.

> **`Type Id` is a key, not a label.** Placements refer to it by name. Rename it and every placement of that type is orphaned, and the export reports them as `unknown type`. Change the label in `Display Name` instead.

> **`Footprint Cells` is the space the object needs empty, not the size of the mesh.** A two-by-three wardrobe is `(2, 3)`, and all six cells must be free. The mesh itself can be any size: the plugin does not measure it.

> **Keep `Allowed Anchors` narrow.** Only `Floor` is set here, and not by accident: the `Free` anchor fits anywhere empty, which effectively turns the check off. A type carrying `Free` will bury the whole volume of air under a generation pass — see 8.3.

### 7.2 The spawn asset

**Miscellaneous → Data Asset → Maze Spawn Asset.** Call it `DA_MazeSpawn_A`. **One per maze.**

- `Library` → `DA_MazeObjectLibrary`
- `Placements` and `Next Id` are read-only, filled in by the brush

Then point the maze at it: `U_Maze_A → Build → Spawns`.

### 7.3 Place some by hand

In the mode panel: `Brush → Tool = Objects`. A palette appears in the `Objects` section — click the `Crate` swatch.

Leave `Brush → Paint Band` at `Play`.

Hover over a cell and read the status line at the bottom of the viewport:

- `Crate fits here (1x1 cells needed, mesh may be bigger)` — it will go there;
- `Crate WON'T FIT (1x1 cells needed): ...` — the geometry refuses, and it says why;
- `Crate OVERLAPS Lamp here (1x1 cells needed)` — it will land on something.

The colour of the frame under the cursor says the same thing: **green** it fits, **red** there is nothing to attach to, **amber** it lands on something already there. The brush does not forbid an overlap — sometimes that is exactly what you want. It just does not keep quiet about it.

LMB places, Ctrl+LMB erases.

**Check.** The `Objects` section reads `Placing: Crate | placed 3, of them never exported 3`.

### 7.4 Build with objects

`Build → Apply Changes`.

**Check.** In the log:

```
LogMazeForge: Export: rooms N, levels N, ...; objects 3 (snapped 3, re-anchored 0, skipped 0, unknown type 0, facing mode without a wall 0); ...
```

The crates stand in the rooms, and in the Outliner they are under `Levels/A/L_A_R_XXX/MazeObjects`.

---

## 8. Objects: the generator and its rules

You do not want to place a hundred crates by hand.

**Miscellaneous → Data Asset → Maze Spawn Rules Asset.** Call it `DA_MazeSpawnRules`. One per project: how thickly crates are strewn is a decision about the game, and ten mazes should be able to share one answer.

### 8.1 A first rule

Add a rule to `Rules`:

| Field | Value | Meaning |
|---|---|---|
| `Target` | `Category` | the rule is about a whole category |
| `Category` | `Decor` | any type of that category |
| `Min Per Room` | `2` | at least two per room |
| `Max Per Room` | `5` | at most five |
| `Min Spacing Cells` | `2` | empty cells between objects |
| `Band` | **`Background`** | the band — see 8.2, this is the decision that matters |
| `Rooms` | leave alone | the room filter; by default, every room |

Leave `Seed` at `1337`. Two runs with the same seed produce the same arrangement — which is what lets you judge a layout, adjust the rules and judge it again.

Now in the mode panel: `Objects → Spawn Rules` → `DA_MazeSpawnRules`, then **Generate Objects**.

**Check.** In the log:

```
LogMazeForge: Generate: objects 34 placed, 0 from the previous run removed; N rooms, N rule runs;
0 rooms short of their minimum, 0 rooms filtered out, 0 rules matched no room, 0 rules match nothing in the library
```

Then `Apply Changes` to get the objects into the levels.

### 8.2 Which band to put them in

The most important decision in a rule, and the most common mistake. The band decides not only where the object sits in depth but **whether it can stand there at all**.

| Band | What goes there | Filled by default | Is there a floor |
|---|---|---|---|
| `Background` | decor behind the player: barrels, crates, pipes | **yes** | yes, the same floors as in play |
| `Play` | whatever the player interacts with | yes | yes |
| `Foreground` | decor in front of the player | **no** | **no** |

**`Background` is the default band for decor.** The player never touches it: there is collision only in `Play`. The generator will scatter barrels there across the same floors as in the play plane, and not one of them will end up in his way.

**`Play` is chosen deliberately.** Anything the generator puts there becomes an obstacle — right for a crate you have to walk around, entirely wrong for background clutter. If the character keeps bumping into barrels after a generation pass, you put decor in `Play`.

**`Foreground` is empty by default, and that is not an oversight.** An empty near plane is the "cutaway" of the original Saboteur and your workspace for hand-placed foreground decor. Since there is no mass there, there are no floors: the `Floor` anchor will find nothing anywhere and the rule returns zero. Two ways out — place only `Free`-anchored objects there and tune them with `Offset`, or switch on `Grid → Depth → Fill → Fill Foreground` and lose the cutaway.

> **The band has to contain mass.** A rule aimed at an unfilled band gives `objects 0 placed` and `N rooms short of their minimum` — true, and no help at all. The brush answers it properly: set `Paint Band` to the same band, pick the type in the palette, hover over a cell above a floor. `no mass in the row below` means there is nothing to stand on in that band.

> **The 2D map does not show depth.** Markers of every band are drawn in the same screen plane: you are looking along the Y axis, so a background barrel lands exactly where a play-band one would. You cannot judge the band from the picture — only from the `Band` field and the status line. A background object that looks sunk into a wall on the map is most likely standing correctly, just behind it.

### 8.3 Setting the type up for a band

The rule itself knows nothing about anchors. Where an object may stand is a property of the **type** in the library, shared by every rule and every maze.

| You want | In the type |
|---|---|
| standing on the floor | `Allowed Anchors` = `Floor` only |
| hanging from the ceiling | `Ceiling` only |
| on a wall — a torch, a lever | `Wall` only |
| floating — smoke, motes | `Free` only |

**The mask is "what it may hold on by", not "what to prefer".** The generator walks the cells of the room and takes, for each one, the first anchor that fits, in the order `Floor → Ceiling → Wall → Free`. That order works inside a cell, not across the maze: a cell under a slab fails `Floor`, passes `Ceiling`, and becomes a legitimate candidate. Leave both floor and ceiling in the mask and you get barrels standing and hanging, mixed together.

> **`Free` turns the check off.** It fits anywhere empty, so an object carrying it will fill the whole volume of air. It also puts the object in the centre of the cell rather than on an edge, so such a barrel ends up half a cell above its floor-standing siblings. Keep `Free` for things that are meant to hang.

The other fields of the type worth checking before a first generation pass:

- **`Footprint Cells`** — how many cells must be **clear**. Not the size of the mesh: the plugin does not measure it. A barrel is `(1, 1)`, or `(1, 2)` if it is tall. It is the bottom of that rectangle that looks for a floor.
- **`Scale`** — the size the actor spawns at. Set it here rather than in the level: the export rebuilds objects on every `Apply Changes`, so a size stretched by hand survives exactly until the next one.
- **`Snap To Anchor Surface`** — leave it on. It measures the actor and presses its edge against the surface, which is why a lamp pivoted at its base does not grow into the ceiling.
- **`Category`** — what a category-targeted rule will catch it by.

### 8.4 Several rules for one type

A type can have as many rules as you like, and that is the main way to get an arrangement worth looking at.

```
Rule 1:  Target=Type, TypeId=Barrel, Band=Background, 4..8 per room
         background barrels, plenty of them, pure texture

Rule 2:  Target=Type, TypeId=Barrel, Band=Play, 0..1 per room,
         Rooms → MaxExits=1
         one barrel as an obstacle, and only in dead ends
```

One type in the library, two entirely different roles in the game. Densities separate the same way: thick clutter in the background, rare objects in the play plane.

### 8.5 What the generator will not do

- **Running it again is safe.** It removes what the previous run scattered and touches nothing placed by hand. Objects you placed yourself are ground that is already taken.
- **It never touches transition points**, even when a rule about the `System` category formally covers them. It says so in the log and skips them.
- **`Clear Generated Objects`** removes only what was scattered. **`Clear All Objects`** removes everything, including what you placed by hand.
- **The snapshot (`Snapshot → Save` / `Restore`) holds only the drawing.** Objects are not in it, and `Restore` will not bring them back.

---

## 9. Maze 2

Repeat §4 in full, changing three things:

| What | Value |
|---|---|
| Asset name | `U_Maze_B` |
| `Build → Maze Name` | **`B`** |
| `Build → Spawns` | a new `DA_MazeSpawn_B` |
| `Grid → World Origin` | `(30000, 0, 0)` — move it along X |

`DA_MazeBuildSettings`, `DA_MazeObjectLibrary` and `DA_MazeSpawnRules` are the same assets — they are shared.

> **About `World Origin`.** It shifts the whole maze in the world. Nothing enforces giving mazes distinct coordinates: the old maze is fully unloaded before the new one arrives, so overlapping is not fatal. But distinct origins make "where am I" answerable, and let both be held for a moment if the transition wants a crossfade. Move the second one far enough that they cannot overlap: `256 × 100 = 25600` units wide, so `30000` leaves room.

To switch to the second maze, pick `U_Maze_B` in `Target → Target Asset`. The first one is taken off the map automatically, which is intended: otherwise two mazes would stand inside each other.

Build it: `Apply Changes`, `Ctrl+Shift+S`.

**Check.** A second set of folders appears on disk:

```
/Game/MazeForge/Maps/B/L_B_R_000_000 ...
/Game/MazeForge/Maps/B/DA_MazeWorldManifest_B
/Game/MazeForge/Meshes/B/SM_B_R_000_000_Play ...
```

If everything landed in the same files as `A`, you forgot `Maze Name`. Go back, set it, and press `Apply Changes` again.

---

## 10. The transition between mazes

A transition is a **pair of points**: a `Gate` in one maze and an `Entry` in another. The door you walk into, and the place you appear.

The key idea: **the door does not know where it leads.** It knows only which door it is — its own placement id — and asks the world graph. That is why one `BP_Door` serves every door in the game.

### 10.1 Two library types

Add two more entries to `DA_MazeObjectLibrary`. **Two for the whole game**, not a pair per doorway.

**Gate** — the door you walk into. This one is a thing; you can see it.

| Field | Value |
|---|---|
| `Type Id` | `Gate` |
| `Category` | `System` |
| `Actor Class` | `BP_Door` (next step) |
| `Allowed Anchors` | `Floor` |
| `Footprint Cells` | `(1, 2)` |
| `Transition Role` | **`Gate`** |
| `Scale` | the size of the door's volume, e.g. `(3, 18, 8)` |

**Entry** — where you arrive. A coordinate, not a thing.

| Field | Value |
|---|---|
| `Type Id` | `Entry` |
| `Category` | `System` |
| `Actor Class` | **empty** |
| `Allowed Anchors` | **`Free`** |
| `Footprint Cells` | `(1, 1)` |
| `Transition Role` | **`Entry`** |

> **Why `Entry` has no class and the `Free` anchor.** An arrival point is a coordinate in mid-air. Any other anchor would demand that it rest against something and would refuse to place it. There is nothing to spawn for it: what the export "builds" for it is an entry in the manifest.

> **Why `Gate`'s `Scale` is set here and not in the level.** The export owns every actor it makes: each `Apply Changes` destroys them and builds them again from the type. A size typed into the level survives exactly until the next `Apply` — and then vanishes silently. Set here, it comes back every time. The export will also notice a hand-stretched actor and say so in the log.

### 10.2 The door blueprint

Create `BP_Door` from `Actor`:

1. **Add Component → Box Collision**. Do not touch its size — the type's `Scale` drives it.
2. Set `Collision Presets` to `OverlapOnlyPawn` so the volume catches only the character.
3. In the Event Graph: from the box's `On Component Begin Overlap` → the **Take Transition For** node.
4. `Traveller` ← `Other Actor` from the overlap. Leave `Gate` alone; it defaults to `Self`.

That is all. Three pins and no destination.

> **Check that the execution chain actually reaches the node.** An unfinished chain in a blueprint compiles without a single warning. The longest transition debugging session in this plugin's history ended with the discovery that `Take Transition For` simply was not wired up, while a `Print String` beside it printed away happily.

The node works the rest out: it takes the door's number off the door's own component, finds the streaming component on whoever walked in, and asks the graph. And on every path where it can fail it says so in the log — precisely because the three-node version failed silently.

### 10.3 Place the points

**Both points are placed with `Paint Band = Play`.** Otherwise the transition will never work: the player exists only in the `Play` band, he cannot walk into a door in the background, and he would arrive on a plane he does not move in.

In maze `A`:
- `Target → Target Asset = U_Maze_A`
- `Brush → Tool = Objects`, `Paint Band = Play`
- palette → `Gate` → place it near the right edge
- palette → `Entry` → place it near the left edge (this is where you come back from `B`)
- `Apply Changes`

In maze `B`, the mirror image: `Entry` on the left, `Gate` on the right.

**Check.** After each `Apply Changes`:

```
LogMazeForge: Export: 2 transition points published — 1 gates, 1 entries. Their ids are what the world graph links.
```

If you get a warning reading `is in the Background band, not Play` instead, erase the point and place it again in the right band.

---

## 11. The world graph and the world map

### 11.1 The graph

**Miscellaneous → Data Asset → Maze World Graph.** Call it `DA_MazeWorld`. One per project.

Add the two manifests to `Mazes`:

```
/Game/MazeForge/Maps/A/DA_MazeWorldManifest_A
/Game/MazeForge/Maps/B/DA_MazeWorldManifest_B
```

Do not fill in `Links` by hand — they are drawn on the map.

Point two things at the graph:
- the mode panel: `World → World Graph` → `DA_MazeWorld`;
- the character blueprint: the `Maze Streaming` component → `World Graph` → `DA_MazeWorld`.

### 11.2 The map

Mode panel: `World` → **Open World Map**. The window opens already knowing your graph.

Controls:

| Action | How |
|---|---|
| link | click a **blue** square (a gate), then a **red** one (an entry) |
| cancel a pending link | `Escape` |
| select a link | click the line |
| delete a link | select it and press `Delete` |
| move a maze | drag its rectangle |
| zoom | wheel |
| pan | middle or right drag |
| frame everything | the `Fit` button or the `F` key |

The status line at the bottom always says what is going on and what to do next.

Link them: the gate in `A` → the entry in `B`. Then the gate in `B` → the entry in `A`.

> **A link is one-way, deliberately.** A door you can walk through both ways is two links, because that is what it is. The way back does not have to arrive where the way there set off.

**Check.** In the log:

```
LogMazeForge: World map: DA_MazeWorldManifest_A gate 12 now leads to DA_MazeWorldManifest_B entry 7.
```

And the status line reads `2 mazes, 2 links, 100%`.

### 11.3 Attach both mazes to the map

Mode panel: `World` → **Attach All Mazes In World Graph**.

This button builds, exports and re-bakes nothing — it only makes the open map agree with the graph: every level of every maze in the graph ends up attached.

`Ctrl+Shift+S`.

**Check.** The **Levels** panel shows levels of both mazes — `L_A_R_...` and `L_B_R_...`.

---

## 12. Checking the transition

Start PIE in maze `A` and walk into the door.

**Check.** In the log, in this order:

```
LogMazeForge: Streaming: transition started; moving in 0.25 s.
LogMazeForge: Streaming: switching to manifest .../DA_MazeWorldManifest_B, entry at X=... Y=... Z=...
LogMazeForge: Streaming: now reading manifest .../DA_MazeWorldManifest_B (was .../DA_MazeWorldManifest_A).
LogMazeForge: Camera bounds set from the manifest: X ...
LogMazeForge: Streaming: room R_000_000 (...) is up — the maze is ready, 0.62 s after the door.
```

On screen: the world fades out, and then you are in the other maze.

Look at the `Y=` in the `entry at` line. If it is not zero — strictly, not the centre of the `Play` band — the arrival point is in the wrong band. Go back to §10.3.

**If the log says nothing at all**, the transition was never called. Look at the door blueprint: almost certainly the execution chain does not reach `Take Transition For`.

---

## 13. When it does not work

The plugin talks. Nearly every failure names itself in the Output Log. Filter by `MazeForge` and read — the messages are written for a person and usually say what to do.

### The character is stuck or falls through at PIE start

The one case the log says nothing about, because the plugin has nothing to do with it: `Player Start` is inside the geometry, or in the wrong depth band. See §4.6 — `Location → Y` must be `0`.

Standing still and immovable means he is in the mass. Falling through the floor means he is in `Background` or `Foreground`, where there is no collision.

### Nothing streams in game

| Log line | What to do |
|---|---|
| `has no manifest ... Nothing will stream.` | the `Manifest` field on the component is empty |
| `has no streaming rules ... Nothing will stream.` | the `Rules` field is empty |
| `manifest ... lists no rooms` | the manifest belongs to another maze, built under a different `Maze Name` |
| `room ... does not exist on disk` | the same thing: this manifest is not from this maze |
| `room ... exists but is not attached to the persistent level` | press `Attach All Mazes In World Graph` and save the map |
| `room ... has its eye closed in the Levels panel` | open the eye in the Levels panel |

### The door does nothing

| Log line | What to do |
|---|---|
| silence | the blueprint chain does not reach the node |
| `was given no gate` | the `Gate` pin was unwired; put `Self` back |
| `carries no placement id` | the type has no `Transition Role = Gate`, or the maze has not been built since the point was placed |
| `has no MazeForge Streaming component` | something other than the character walked in: use `OverlapOnlyPawn` |
| `has no World Graph` | the `World Graph` field on the component is empty |
| `gate N ... leads nowhere` | the link was never drawn, or the point was re-placed and took a new number |
| `is in the Background band, not Play` | the point is in the background; erase it and place it again |

### Objects

| Symptom | Cause |
|---|---|
| `unknown type` in the export report | a `Type Id` was renamed in the library after the objects were placed |
| a lamp inside the ceiling | `Snap To Anchor Surface` is off on that type |
| an object facing backwards | a flat single-sided mesh — it needs a `Two Sided` material, not a code change |
| `had been resized by hand` | the size was stretched in the level; set `Scale` on the type |
| `rules matched no room` | the room filter matched nothing |

### Building the second maze made the first disappear

At least one of them has no `Maze Name`. Give both a name and rebuild both. The old assets stay on disk as orphans — delete them by hand, once.

---

## 14. Cheat sheet

**The order to create assets for a two-maze world:**

```
1. DA_MazeBuildSettings        once, change nothing
2. DA_MazeStreamRules          once, change nothing
3. DA_MazeObjectLibrary        once: Crate, Gate, Entry
4. DA_MazeSpawnRules           once: one rule about Decor
5. U_Maze_A + DA_MazeSpawn_A   Maze Name = A
6. U_Maze_B + DA_MazeSpawn_B   Maze Name = B
7. DA_MazeWorld                both manifests in Mazes
```

**Links that are easy to forget:**

- `U_Maze_* → Build → Build Settings` — otherwise the export falls back on hard-coded defaults and your settings asset is not used at all;
- `U_Maze_* → Build → Spawns` — otherwise there is nowhere to put objects;
- `DA_MazeSpawn_* → Library` — otherwise placements refer to nothing;
- the character's component → `Manifest`, `Rules`, `World Graph` — **by hand, and never updated for you**;
- the mode panel → `World → World Graph` — for the `Attach All Mazes` button;
- `Player Start` — move it into an empty cell with `Y = 0` after each maze is first built.

**What `Apply Changes` does:** snapshot → volume → slicing → meshes → levels and manifest → attach to the map. Then `Ctrl+Shift+S`.

**Keys in the mode:** `Q` lifts the layer dimming while held, `PgUp` / `PgDn` change the active depth slice, `Ctrl` + drag fills a rectangle, `Ctrl` + LMB erases.

**Keys on the world map:** `F` frames everything, `Delete` removes the selected link, `Escape` cancels a pending link.

**Console:** `MazeForge.DebugStreaming 1` shows the pool on screen. `MazeForge.DumpStreamCSV` dumps a load profile to CSV.

**Three rules that will save you an evening:**

1. Set `Maze Name` before the first build, as soon as there are two mazes.
2. Place transition points only with `Paint Band = Play`.
3. The manifest on the character is a manual link. Rebuild a maze under a different name and you have to carry it over yourself.
