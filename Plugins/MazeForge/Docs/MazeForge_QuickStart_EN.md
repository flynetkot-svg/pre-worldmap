# MazeForge — Quick Start

**What you get:** a world of two mazes with a working transition between them.
**Version:** MazeForge 0.5.0 · Unreal Engine 5.8.2
**Field-by-field reference:** `MazeForge_Reference_EN.md`
**По-русски:** `MazeForge_QuickStart_RU.md`

This document is one straight path with no branches. Do it in order and skip nothing. Every stage ends with a **check** — a short sign that the step worked. If a check does not match, stop there: it only gets worse further on.

Anything not mentioned here, leave alone. The defaults are chosen for a first maze.

---

## Contents

1. [Three things to understand first](#1-three-things-to-understand-first)
2. [The cell grid and the room grid](#2-the-cell-grid-and-the-room-grid)
3. [Installation](#3-installation)
4. [The map of assets](#4-the-map-of-assets)
5. [Maze 1: draw it and build it](#5-maze-1-draw-it-and-build-it)
6. [The character: hooking up streaming](#6-the-character-hooking-up-streaming)
7. [Checking it in game](#7-checking-it-in-game)
8. [Objects: the library and the brush](#8-objects-the-library-and-the-brush)
9. [Objects: the generator and its rules](#9-objects-the-generator-and-its-rules)
10. [Maze 2](#10-maze-2)
11. [The transition between mazes](#11-the-transition-between-mazes)
12. [The world graph and the world map](#12-the-world-graph-and-the-world-map)
13. [Checking the transition](#13-checking-the-transition)
14. [When it does not work](#14-when-it-does-not-work)
15. [Cheat sheet](#15-cheat-sheet)
16. [Generating a maze](#16-generating-a-maze)
17. [Reachability, doors and keys](#17-reachability-doors-and-keys)
18. [Ladders](#18-ladders)
19. [Moving a maze to another project](#19-moving-a-maze-to-another-project)
20. [Floors](#20-floors)
21. [Hewn rock: the Rough brush](#21-hewn-rock-the-rough-brush)
22. [Lifts](#22-lifts)

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

## 2. The cell grid and the room grid

A maze has two grids, one over the other. The **cell grid** is what it is made of. The **room grid** is the size of the pieces it streams in as. You paint the first and colour in the second. This section covers how they work, the order a level is built in, and how to avoid doing the same work twice.

### 2.1 The cell grid

A maze is a voxel grid. A cell is a cube of `Grid → Cell Size`, `100` units by default — one metre. `Grid → Size XZ` is the extent in cells: X along the level, Z up. Depth comes from the `Grid → Depth` profile: how many cells go into `Background`, `Play` and `Foreground`.

Only occupied cells are stored; emptiness is the absence of a cell. You draw in a single plane, in the middle of the `Play` band. It is grown into a volume along the depth profile by `Apply Changes`.

For a sense of scale:

| Map | Size | Cells in the volume | Full `Apply` | `Apply` with no changes |
|---|---|---|---|---|
| tutorial | 256×128, depth 3/2/2 | tens of thousands | seconds | a second |
| original Saboteur | 707×376, depth 9/2/9 | 1.9 million | ~40 s | ~5 s |

On a large map check one field: `DA_MazeBuildSettings → Rooms Per Flush` must be above zero (`16` by default). The build unloads finished rooms in batches of that many. At zero it keeps everything loaded at once, and on a map the size of Saboteur runs out of GPU address space — the editor dies after the build has already succeeded.

### 2.2 The room grid

In game the maze is not loaded whole but in **rooms**. Each room is a level of its own with its own meshes, one per depth band. Only the rooms around the player are kept in memory — up to nine by default.

The slicer, `Slicing → Slicer`, cuts the maze into rooms; by default it is `Uniform Grid`, which lays a lattice over the map:

| Slicer field | Meaning |
|---|---|
| `Room Size XZ` | lattice step in cells: X along the level, Y is height |
| `Origin XZ` | lattice offset in cells |
| `Merges` | fused lattice cells, see 2.3 |
| `Discard Empty Rooms` | a lattice cell without a single solid cell does not become a room |

Every lattice cell is a room named `R_XXX_ZZZ`: the column and row of the lattice. The numbers have gaps — those are empty cells that were thrown away. That is expected.

**Choosing the size.** A room should be roughly a screen, a little more. Large rooms drag a lot of unneeded geometry into memory and load noticeably. Small ones mean hundreds of levels, frequent loads and corridors chopped into pieces. A sensible range is 24×12 to 32×32 cells.

The best approach is **a fine lattice plus merging**. Set a small step, then assemble rooms of the shape you want from its cells (2.3). On Saboteur a 24×12 lattice gave 697 cells, which colouring turned into 124 rooms.

> **Set `Room Size XZ` and `Origin XZ` before colouring rooms, and leave them alone after.** Merges are stored as lattice cell numbers. Change the step or the offset and every merge silently lands somewhere else. If you do have to change them: `Advanced → Clear All Room Merges` first, then the field, then colour again.

### 2.3 Merging rooms: the Rooms tool

The lattice cuts the map into identical rectangles; a maze wants rooms that make sense — a lift shaft the full height, a long corridor as one strip, a hall in one piece. You get them by merging lattice cells.

`Brush → Tool = Rooms`. Room frames get thicker.

| Action | What it does |
|---|---|
| drag with LMB | every lattice cell in the rectangle becomes one room |
| drag with **Shift** | split: remove the merges in the rectangle |

Merged rooms are hatched, each in its own colour. The hatching shows only in `Rooms` mode and only until `Apply` — it is a draft of the layout, not the built result. The rectangle under the cursor while dragging is hatched the other way, so it stands out over what is already merged.

The viewport status line always tells you what the mouse will do and keeps count: `merges N` is how many merges there are, and `N ROOMS AFTER APPLY` shows how many rooms you will get while the layout is not applied yet.

Merge rules:

- **Merges never overlap.** A new one absorbs every merge it touches. Neighbours that only share an edge stay separate rooms.
- **A merged room is named after its bottom-left cell.** If that cell used to be empty, you get a room with a new name, and the old rooms inside it are retired. The log says so while you drag.
- **Nothing is built until `Apply Changes`.**

Also at hand:

| Button | Where | What it does |
|---|---|---|
| **Show / Hide Room Levels** | `Display` | hide the built levels to see the lattice and the hatching without geometry |
| **Clear All Room Merges** | `Advanced` | reset the whole colouring |

How the hatching looks — spacing, cross-hatch, opacity, colours — is set in the style preset (`Display → Style`): `Room Hatch Spacing Cells`, `Room Hatch Crossed`, `Room Hatch Opacity`, `Room Merge Palette`.

> **Why merge whatever spans several rooms.** A lift, a long ladder, a rope across a gap each live in exactly one level — the one their start is in. That level unloads and the object vanishes whole, with the player on it. Merge the cells it crosses into one room and the problem stops existing instead of being managed. A shaft is thin and nearly empty, so such a room costs almost nothing. Merging half the map, on the other hand, throws streaming away.

### 2.4 How a level is built

The order in which no work gets done twice:

1. **The asset.** `Maze Name`, `Build Settings`, `Size XZ`, `Depth`, `Room Size XZ` — all before the first stroke (§5.2).
2. **The plane.** `Generate Maze` or the brush, `Tool = Cells` (§5.4).
3. **Rooms.** `Tool = Rooms`, colouring (2.3).
4. **Build.** `Apply Changes`, then `Ctrl+Shift+S`.
5. **Objects.** By hand (§8) and/or with the generator (§9), then `Apply Changes` again.
6. **Edits.** `Edit → Change Current Maze`, the brush, `Edit → Apply Changes To Current Maze`.

**`Apply Changes` rebuilds only what changed.** Every room keeps a fingerprint of what it is made of:

- meshes are rebuilt only for rooms where something changed, plus the neighbour across the seam when the edit sits right at the boundary;
- a level is rewritten only when what it holds changed: its meshes, its neighbours, the room's objects or their library types. The other levels are not loaded or touched at all — and neither is any decoration placed in them by hand.

A second `Apply` in a row with no edits looks like this:

```
LogMazeForge: Export: rooms 124, levels written 0, unchanged and left alone 124, ...
LogMazeForge: Apply changes: 124 rooms, 0 rebuilt, 124 left alone, 0 levels written, 0 levels attached; in 4.96 s.
```

And `git status` after it shows not a single modified `.umap`.

**Re-slice, and the old rooms leave the stage.** If some rooms no longer exist after a new colouring, `Apply` takes their levels off the map and removes their Outliner folders:

```
LogMazeForge: Retire: rooms taken off the map 1 (detached 1, outliner folders 3). ... Rooms: R_027_025
```

Their level and mesh files stay where they are. Getting them out of sight is a separate button, **`Build → Move Stale Rooms To Deprecated`**: it moves everything stale into a `Deprecated` folder next to the live levels and meshes. Delete them from there by hand, or bring them back. It is a button of its own rather than part of `Apply` because moving assets in the engine is slow: on a hundred rooms it would add minutes to every build.

**When to rebuild everything from scratch.** Fingerprints cannot see meshes or levels on disk being deleted or edited by hand. For that there is `Advanced → Force Full Rebake`: it rewrites every mesh and every level. The tick is not remembered between editor sessions, and while it is on the build reminds you in the log. Untick it after the run you ticked it for.

### 2.5 Identical mazes: copy the room layout

If a second maze repeats the first one's geometry, there is no need to colour its rooms again.

1. Open the source asset and find the **`Slicer`** field in Details.
2. Right-click the field's header → **Copy**.
3. Open the target asset, same field → right-click → **Paste**.
4. `Apply Changes` on the target.

`Room Size XZ`, `Origin XZ` and every `Merges` entry come across in one go.

There is one condition: both mazes have the same `Size XZ`, `Room Size XZ` and `Origin XZ`. Otherwise the merges land in the wrong places. Check after pasting: `merges N` in the status line should match the source, and the hatching in `Rooms` mode should fall in the same places.

> **Need a full copy, drawing included? `Duplicate` the asset in the Content Browser and change `Maze Name` and `Spawns` straight away.** A duplicate with the same `Maze Name` writes to the same levels, the same meshes and the same manifest, and its first build overwrites the original.

---

## 3. Installation

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

## 4. The map of assets

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

## 5. Maze 1: draw it and build it

### 5.1 Two assets you create once

Both are shared across the project and neither needs filling in — only creating. Do it now so they are already to hand later.

**Build settings.** Content Browser → right-click → **Miscellaneous → Data Asset → Maze Build Settings.** Call it `DA_MazeBuildSettings`.

**Change nothing inside.** Every default works: levels land in `/Game/MazeForge/Maps`, meshes in `/Game/MazeForge/Meshes`, and the palette is filled in by the constructor. The one thing worth doing later is putting your own materials into `Palette`. You do not need it for a first maze: with no materials the geometry comes out grey, but correct.

**Streaming rules.** Content Browser → right-click → **Miscellaneous → Data Asset → Maze Streaming Rules Asset.** Call it `DA_MazeStreamRules`.

Change nothing inside this one either: both the rule set and the budgets are created by the constructor. You will need it in §6, when you hook up the character — but it is easier to make it now, alongside its neighbour.

### 5.2 The maze itself

**Content Browser → right-click → MazeForge → Maze Grid.**

Call it `U_Maze_A`. Open it and fill in:

| Field | Value | Why |
|---|---|---|
| `Grid → Cell Size` | `(100, 100, 100)` | the standard UE cube |
| `Grid → Size XZ` | `(256, 128)` | plenty for a first try |
| `Grid → Depth` | `3 / 2 / 2` | Background / Play / Foreground |
| `Grid → World Origin` | `(0, 0, 0)` | where the maze sits in the world, see §10 |
| `Build → Maze Name` | **`A`** | see below — this matters |
| `Build → Build Settings` | `DA_MazeBuildSettings` | **required** |
| `Generation → Generator` | `Random (Saboteur-style complex)` | or leave whatever it was created with |
| `Slicing → Slicer` | `Uniform Grid`, `Room Size XZ = (32, 32)` | as is for a first go; how to choose — §2.2 |

Leave the rest alone.

> **Fill in `Maze Name` right away.** An empty name is the "one maze in the project" behaviour. Room ids are not unique between mazes: every slicing starts at `R_000_000`. Two mazes with empty names write the same level names, the same mesh names and **one manifest between them** — building the second silently overwrites the first. You are making a world of two mazes, so both need a name. Anything that is not a letter, a digit or an underscore is stripped: the name becomes part of a package path.

There are deliberately no buttons inside the asset: it holds parameters, and the work happens in the mode panel.

### 5.3 The editing mode

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

### 5.4 Draw

Switch to the **Left** or **Right** view — the view menu is in the top-left corner of the viewport.

Two ways, take either:

**Generate it.** The `Generate` section → **Generate Maze**. The grid fills according to the generator set on the asset. The button is disabled while the generator is `Manual` — the caption under it says so.

**Draw it.** The `Brush` section, `Tool = Cells`, `Paint Type = Solid`. LMB paints, Ctrl + drag fills a rectangle. Holding `Q` temporarily lifts the dimming of the inactive layers.

Generating is enough for a first pass.

**Check.** `Status` now reads `plane · cells <a lot>`.

### 5.5 Build

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
LogMazeForge: Export: rooms N, levels written N, unchanged and left alone 0, meshes ...
LogMazeForge: Levels in /Game/MazeForge/Maps/A, manifest /Game/MazeForge/Maps/A/DA_MazeWorldManifest_A
LogMazeForge: Apply changes: N rooms, N rebuilt, 0 left alone, N levels written, N levels attached; in 1.6 s. Save everything — Ctrl+Shift+S.
```

And the viewport now shows real geometry instead of preview cubes.

**Save everything: Ctrl+Shift+S.** The export writes its assets to disk itself, but attaching the levels changes the open map, and that has to be saved by hand.

> The log will carry lines reading `LogSpawn: Warning: UWorld::DestroyActor: World has no context!`, one per rebuilt actor in every **rewritten** level. That is a false positive from the engine about attached sublevels. Nothing is broken; ignore them. Levels that did not change are not touched by the build (§2.4), and produce none of these lines.

### 5.6 Move the Player Start

The project template put `Player Start` wherever suited it — and you have just built over that spot. The character will end up inside the mass and either stick fast or fall through.

In the **Left** or **Right** view, find an empty corridor and put `Player Start` in it. Then select it and check one number in Details:

| Field | Value |
|---|---|
| `Transform → Location → Y` | **`0`** |

Zero on Y is the centre of the `Play` band, the only one the player exists in. Looking at the viewport will not tell you: from the side all three bands overlap exactly, and a `Player Start` that has drifted into the background looks like it is standing in the corridor. The number in Details is the only way to see it.

Set X and Z by eye: an empty cell, preferably with a floor under it.

**Check.** Start PIE. The character stands in a corridor and walks, rather than falling or stuck in a wall. If he falls through the floor you put him in `Background` or `Foreground`: there is collision only in `Play`.

---

## 6. The character: hooking up streaming

Rooms are streamed around an **observer** — an actor carrying the component. Normally the player character.

1. Open the character blueprint.
2. **Add Component → Maze Streaming**.
3. Select the component and fill in, in Details:

| Field | Value |
|---|---|
| `Manifest` | `/Game/MazeForge/Maps/A/DA_MazeWorldManifest_A` |
| `Rules` | `DA_MazeStreamRules` — the one you made in §5.1 |
| `World Graph` | leave empty for now; §12 fills it in |

Leave the other fields alone. Their defaults work: the camera widens to the size of the maze, the screen fades on a transition, and the timings are set.

> **The plugin's main trap.** The maze asset has a field `Build → Built Manifest (output)`. It is **greyed out and read-only**, and the game does **not** read it. The runtime reads `Manifest` here, on the component, and that field is filled in by hand once. The two never follow each other. Build a maze under a new `Maze Name` and its manifest becomes a different asset — carrying it over here is still a manual step.

**Check.** The component appears in the Components list and `Manifest` is not empty.

---

## 7. Checking it in game

Start PIE.

**Check.** In the Output Log, right after start:

```
LogMazeForge: Streaming: BP_YourCharacter_C_0 reads manifest /Game/MazeForge/Maps/A/DA_MazeWorldManifest_A
LogMazeForge: Streaming: now reading manifest ... (was none).
LogMazeForge: Camera bounds set from the manifest: X 0..25600
```

Run around — rooms load and unload in silence. To watch it happen, type `MazeForge.DebugStreaming 1` in the console: the screen shows every room and how much the pool wants it loaded.

If you get an `Error` instead, see §14 — all three cases are covered there.

---

## 8. Objects: the library and the brush

### 8.1 The library

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

> **Keep `Allowed Anchors` narrow.** Only `Floor` is set here, and not by accident: the `Free` anchor fits anywhere empty, which effectively turns the check off. A type carrying `Free` will bury the whole volume of air under a generation pass — see 9.3.

### 8.2 The spawn asset

**Miscellaneous → Data Asset → Maze Spawn Asset.** Call it `DA_MazeSpawn_A`. **One per maze.**

- `Library` → `DA_MazeObjectLibrary`
- `Placements` and `Next Id` are read-only, filled in by the brush

Then point the maze at it: `U_Maze_A → Build → Spawns`.

### 8.3 Place some by hand

In the mode panel: `Brush → Tool = Objects`. A palette appears in the `Objects` section — click the `Crate` swatch.

Leave `Brush → Paint Band` at `Play`.

Hover over a cell and read the status line at the bottom of the viewport:

- `Crate fits here (1x1 cells needed, mesh may be bigger)` — it will go there;
- `Crate WON'T FIT (1x1 cells needed): ...` — the geometry refuses, and it says why;
- `Crate OVERLAPS Lamp here (1x1 cells needed)` — it will land on something.

The colour of the frame under the cursor says the same thing: **green** it fits, **red** there is nothing to attach to, **amber** it lands on something already there. The brush does not forbid an overlap — sometimes that is exactly what you want. It just does not keep quiet about it.

LMB places, Ctrl+LMB erases.

**Check.** The `Objects` section reads `Placing: Crate | placed 3, of them never exported 3`.

### 8.4 Build with objects

`Build → Apply Changes`.

**Check.** In the log:

```
LogMazeForge: Export: rooms N, levels N, ...; objects 3 (snapped 3, re-anchored 0, skipped 0, unknown type 0, facing mode without a wall 0); ...
```

The crates stand in the rooms, and in the Outliner they are under `Levels/A/L_A_R_XXX/MazeObjects`.

---

## 9. Objects: the generator and its rules

You do not want to place a hundred crates by hand.

**Miscellaneous → Data Asset → Maze Spawn Rules Asset.** Call it `DA_MazeSpawnRules`. One per project: how thickly crates are strewn is a decision about the game, and ten mazes should be able to share one answer.

### 9.1 A first rule

Add a rule to `Rules`:

| Field | Value | Meaning |
|---|---|---|
| `Target` | `Category` | the rule is about a whole category |
| `Category` | `Decor` | any type of that category |
| `Min Per Room` | `2` | at least two per room |
| `Max Per Room` | `5` | at most five |
| `Min Spacing Cells` | `2` | empty cells between objects |
| `Band` | **`Background`** | the band — see 9.2, this is the decision that matters |
| `Rooms` | leave alone | the room filter; by default, every room |

Leave `Seed` at `1337`. Two runs with the same seed produce the same arrangement — which is what lets you judge a layout, adjust the rules and judge it again.

Now in the mode panel: `Objects → Spawn Rules` → `DA_MazeSpawnRules`, then **Generate Objects**.

**Check.** In the log:

```
LogMazeForge: Generate: objects 34 placed, 0 from the previous run removed; N rooms, N rule runs;
0 rooms short of their minimum, 0 rooms filtered out, 0 rules matched no room, 0 rules match nothing in the library
```

Then `Apply Changes` to get the objects into the levels.

### 9.2 Which band to put them in

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

### 9.3 Setting the type up for a band

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

### 9.4 Several rules for one type

A type can have as many rules as you like, and that is the main way to get an arrangement worth looking at.

```
Rule 1:  Target=Type, TypeId=Barrel, Band=Background, 4..8 per room
         background barrels, plenty of them, pure texture

Rule 2:  Target=Type, TypeId=Barrel, Band=Play, 0..1 per room,
         Rooms → MaxExits=1
         one barrel as an obstacle, and only in dead ends
```

One type in the library, two entirely different roles in the game. Densities separate the same way: thick clutter in the background, rare objects in the play plane.

### 9.5 What the generator will not do

- **Running it again is safe.** It removes what the previous run scattered and touches nothing placed by hand. Objects you placed yourself are ground that is already taken.
- **It never touches transition points**, even when a rule about the `System` category formally covers them. It says so in the log and skips them.
- **`Clear Generated Objects`** removes only what was scattered. **`Clear All Objects`** removes everything, including what you placed by hand.
- **The snapshot (`Snapshot → Save` / `Restore`) holds only the drawing.** Objects are not in it, and `Restore` will not bring them back.

### 9.6 Recipes: putting an object where you want it

Four things decide where an object ends up:

| What | Where it is set | Decides |
|---|---|---|
| band | `Band` on the rule, `Paint Band` on the brush | where the object is in depth: background, play plane, foreground |
| anchor | `Allowed Anchors` on the type | what it is held against: floor, ceiling, side wall, nothing |
| snap | `Snap To Anchor Surface` on the type | makes the mesh touch the surface with its own edge, wherever its pivot is |
| offset | `Offset` on the type | fine adjustment in units, first of all in depth (Y) |

Below are five tested combinations. `Offset` values are given for the default depth profile `3/2/2` and a cell of 100; how to recompute them for your profile is explained on the spot.

#### Barrels and crates on the floor, in the background

The main case, and the one that started it all: a barrel standing on the floor behind the player and never in his way.

| Library type | Value |
|---|---|
| `Category` | `Decor` |
| `Allowed Anchors` | **`Floor`** only |
| `Footprint Cells` | `(1, 1)`, a tall barrel `(1, 2)` |
| `Snap To Anchor Surface` | **on** |
| `Facing Mode` | `Random X` — so a row of barrels does not all face one way |

| Rule or brush | Value |
|---|---|
| `Band` / `Paint Band` | **`Background`** |
| `Min / Max Per Room` | `2..6` |
| `Min Spacing Cells` | `1..2` |

Why it stands true: the `Floor` anchor finds a solid cell under the bottom edge of the `Footprint Cells` rectangle and puts the origin on its top face. `Snap To Anchor Surface` measures the actor and lowers it until the bottom of the mesh rests on that face — wherever the mesh's pivot happens to be. The export report counts these in `snapped N`.

The background is filled with the same mass as the play plane, so its floors are in the same places. The player never touches the background: collision exists only in `Play`.

#### Pictures on the back wall

**`Wall` is the wrong anchor here.** The `Wall` anchor is a side wall of a corridor: its left or right face along X, the end of it. The back wall is a separate layer in the far-most depth slice (`Paint Type = Back Wall`, or the `Fill Back Wall` button), and there is no anchor for it. A picture is hung in the air of the background band and moved back to the wall.

| Library type | Value |
|---|---|
| `Allowed Anchors` | **`Free`** only |
| `Footprint Cells` | the picture's size in cells, e.g. `(2, 2)` — that much must be empty in the corridor |
| `Snap To Anchor Surface` | does not matter: `Free` has nothing to snap to |
| `Fixed Rotation` | so the face looks at the camera (+Y); depends on how the mesh was modelled |
| `Offset` | **Y — the shift to the back wall**, Z — height inside the cell |

`Free` puts the origin in the centre of the rectangle, in the slice in the middle of the background band. From there to the front face of the back wall:

```
Offset.Y = −(⌊Background Cells / 2⌋ − Backdrop Cells − 0.5) × Cell Size.Y
```

| Depth profile (`Background Cells`, `Backdrop Cells`) | `Offset.Y` |
|---|---|
| `3`, `0` — the default | **−50** |
| `9`, `0` — as in Saboteur | **−350** |
| `9`, `1` | −250 |

The formula brings the **pivot** to the wall. If the mesh's pivot is in the middle of the frame's thickness, add half the thickness back, or the frame will be half inside the wall.

Place pictures **by hand**, `Paint Band = Background`. With the `Free` anchor the generator considers any empty cell suitable, including the middle of a tall shaft, and will hang pictures in mid-air. If you must use the generator: a narrow room filter, `Max Per Room = 1`, a large `Min Spacing Cells`.

The plugin does not check whether there is a back wall behind the picture. Where none is painted — a window to the sky — the picture hangs in front of nothing.

#### The foreground

The `Foreground` band is empty by default: it is the "cutaway" the camera looks in through (see 9.2). With no mass there is no floor either — the `Floor` anchor finds nothing to rest on anywhere.

| Library type | Value |
|---|---|
| `Allowed Anchors` | **`Free`** only |
| `Footprint Cells` | the space it takes, e.g. a column `(1, 4)` |
| `Offset.Z` | **−(Footprint.Y × CellSize.Z) / 2** — drops the pivot to the bottom of the rectangle; −200 for a `(1, 4)` column |

Place **by hand**, `Paint Band = Foreground`, choosing the cell right above a floor of the play plane: that floor is drawn in the same view, on the same XZ. `Offset.Z` brings the object down from the centre of the rectangle to its bottom, i.e. to floor level — provided the mesh's pivot is at its base.

What goes there: low and sparse things — beams, pipes, railings, columns, bushes along the edge. The foreground covers the player, so each such object is a spot where he cannot be seen. Better to turn off blocking collision on these blueprints: it will not stop the player — he lives at `Y = 0` — but it may get in the camera's way.

The other way is to turn on `Grid → Depth → Fill → Fill Foreground`. Floors appear and `Floor` works, but the near plane turns solid and the cutaway is gone. Usually not worth it.

#### Ceiling lamps

| Library type | Value |
|---|---|
| `Allowed Anchors` | **`Ceiling`** only |
| `Footprint Cells` | `(1, 1)` |
| `Snap To Anchor Surface` | **on** — the top of the mesh meets the ceiling, wherever the pivot is |

| Rule | Value |
|---|---|
| `Band` | **`Background`** |
| `Min / Max Per Room` | `1..3` |
| `Min Spacing Cells` | `4` or more |

`Ceiling` takes a cell with mass above its top edge and hangs the object under it. A lamp pivoted at its base will not grow into the ceiling: that is exactly what `Snap` exists for.

A lamp on a chain meant to hang lower: either make the chain part of the mesh and keep `Snap`, or turn `Snap` off and set a negative `Offset.Z`.

The band is `Background`: light from a lamp in the background still lights the play plane, and the player cannot bump his head on it. Lamps go into `Play` only when the game uses them — say, they can be shot out. Then the blueprint must have no blocking collision, or every jump will hit a lamp.

Light is expensive: keep few lamps per room and turn shadows off where you do not need them.

#### Objects in the play band: health, ammo, obstacles

| Library type | Value |
|---|---|
| `Category` | `Item` for pickups, `Decor` for obstacles |
| `Allowed Anchors` | **`Floor`** only |
| `Snap To Anchor Surface` | **on** |
| `Offset.Y` | **−50** with `Play = 2` (see below) |

| Rule or brush | Value |
|---|---|
| `Band` / `Paint Band` | **`Play`** |
| `Min / Max Per Room` | few: `0..1` or `0..2` |
| `Rooms` | for obstacles, a filter such as `Max Exits = 1` so they stand in dead ends (see 9.4) |

**About `Offset.Y`.** An object in `Play` lands in the middle of the slice the maze is drawn in. With an even number of cells in `Play` (`2` by default) that slice is half a cell closer to the camera than the player's plane `Y = 0`: 50 units at a cell of 100. For an obstacle with thick collision it does not matter. For a pickup with a thin trigger it does: the player walks past without touching it. Give the type `Offset.Y = −CellSize.Y / 2`, or make the trigger deeper than one cell. With an odd `Play` no shift is needed.

Collision decides the role:

- **a pickup** — overlap only (`OverlapOnlyPawn`), no blocking: the player walks through and picks it up;
- **an obstacle** — blocking collision, and `Footprint Cells` tall enough that the generator will not put it under a low ceiling.

Everything in `Play` is something the player bumps into. If after generating he gets stuck at every step, decor ended up in `Play` (see 9.2). Transition points also go in `Play` only (§11.3).

#### Summary

| What | `Band` | `Allowed Anchors` | `Snap` | `Offset` | Place |
|---|---|---|---|---|---|
| barrels, crates | `Background` | `Floor` | on | — | generator |
| pictures on the back wall | `Background` | `Free` | — | Y to the wall, see formula | by hand |
| foreground | `Foreground` | `Free` | — | Z = −height/2 | by hand |
| lamps | `Background` | `Ceiling` | on | Z for a chain | generator |
| health, ammo | `Play` | `Floor` | on | Y = −50 with `Play = 2` | by hand or generator, few |
| obstacles | `Play` | `Floor` | on | — | generator, in dead ends |

---

## 10. Maze 2

Repeat §5 in full, changing three things:

| What | Value |
|---|---|
| Asset name | `U_Maze_B` |
| `Build → Maze Name` | **`B`** |
| `Build → Spawns` | a new `DA_MazeSpawn_B` |
| `Grid → World Origin` | `(30000, 0, 0)` — move it along X |

`DA_MazeBuildSettings`, `DA_MazeObjectLibrary` and `DA_MazeSpawnRules` are the same assets — they are shared.

If the second maze repeats the first one's geometry, do not colour its rooms again — copy the layout, see §2.5.

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

## 11. The transition between mazes

A transition is a **pair of points**: a `Gate` in one maze and an `Entry` in another. The door you walk into, and the place you appear.

The key idea: **the door does not know where it leads.** It knows only which door it is — its own placement id — and asks the world graph. That is why one `BP_Door` serves every door in the game.

### 11.1 Two library types

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

### 11.2 The door blueprint

Create `BP_Door` from `Actor`:

1. **Add Component → Box Collision**. Do not touch its size — the type's `Scale` drives it.
2. Set `Collision Presets` to `OverlapOnlyPawn` so the volume catches only the character.
3. In the Event Graph: from the box's `On Component Begin Overlap` → the **Take Transition For** node.
4. `Traveller` ← `Other Actor` from the overlap. Leave `Gate` alone; it defaults to `Self`.

That is all. Three pins and no destination.

> **Check that the execution chain actually reaches the node.** An unfinished chain in a blueprint compiles without a single warning. The longest transition debugging session in this plugin's history ended with the discovery that `Take Transition For` simply was not wired up, while a `Print String` beside it printed away happily.

The node works the rest out: it takes the door's number off the door's own component, finds the streaming component on whoever walked in, and asks the graph. And on every path where it can fail it says so in the log — precisely because the three-node version failed silently.

### 11.3 Place the points

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

## 12. The world graph and the world map

### 12.1 The graph

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

### 12.2 The map

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

### 12.3 Attach both mazes to the map

Mode panel: `World` → **Attach All Mazes In World Graph**.

This button builds, exports and re-bakes nothing — it only makes the open map agree with the graph: every level of every maze in the graph ends up attached.

`Ctrl+Shift+S`.

**Check.** The **Levels** panel shows levels of both mazes — `L_A_R_...` and `L_B_R_...`.

---

## 13. Checking the transition

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

Look at the `Y=` in the `entry at` line. If it is not zero — strictly, not the centre of the `Play` band — the arrival point is in the wrong band. Go back to §11.3.

**If the log says nothing at all**, the transition was never called. Look at the door blueprint: almost certainly the execution chain does not reach `Take Transition For`.

---

## 14. When it does not work

The plugin talks. Nearly every failure names itself in the Output Log. Filter by `MazeForge` and read — the messages are written for a person and usually say what to do.

### The character is stuck or falls through at PIE start

The one case the log says nothing about, because the plugin has nothing to do with it: `Player Start` is inside the geometry, or in the wrong depth band. See §5.6 — `Location → Y` must be `0`.

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

### Every `Apply Changes` rebuilds every room

The log says `Mesh bake: rooms N (of them unchanged 0)` although you changed nothing, and next to it warns `Force Full Rebake is on`. Untick `Advanced → Force Full Rebake`: it rebuilds everything without looking at the fingerprints.

If there is no such warning, look at the `Bake check` line: `N rooms had none` means there are no fingerprints (the first build after a plugin update or a clean), `N had a different one` means something shared by every room changed: `Build Settings`, the palette, the depth profile, the cell size.

### Levels and folders of rooms that no longer exist

After a re-slice their levels are taken off the map by the next `Apply Changes`, together with their Outliner folders — including ones left behind by older builds. The level and mesh files stay on disk: `Build → Move Stale Rooms To Deprecated` moves them into a `Deprecated` folder.

### Building the second maze made the first disappear

At least one of them has no `Maze Name`. Give both a name and rebuild both. The old assets stay on disk as orphans — delete them by hand, once.

---

## 15. Cheat sheet

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

**What `Apply Changes` does:** snapshot → volume → slicing → meshes → levels and manifest → attach to the map. Rebuilds and rewrites only what changed. Then `Ctrl+Shift+S`.

**Order of work on a level:** asset (`Room Size` right away) → plane → room colouring (`Tool = Rooms`) → `Apply` → objects → `Apply`.

**Floors:** the ground is `Floor` cells — after generation, after each brush stroke, and on `Mark Floor Surfaces` for a finished maze. Give `Palette → Floor` a material, or nothing shows.

**Keys in the mode:** `Q` lifts the layer dimming while held, `PgUp` / `PgDn` change the active depth slice, `Ctrl` + drag fills a rectangle, `Ctrl` + LMB erases. With `Tool = Rooms`: drag merges rooms, `Shift` + drag splits them. With `Tool = Path`: click — a main path point, `Shift` + click — a branch point, `Ctrl` + `Shift` + click — a new branch, `Ctrl` + click — remove. With `Tool = Ladder`: click — the bottom, click — the top, `Shift` + click — cancel, or remove the ladders in a column.

**Keys on the world map:** `F` frames everything, `Delete` removes the selected link, `Escape` cancels a pending link.

**Console:** `MazeForge.DebugStreaming 1` shows the pool on screen. `MazeForge.DumpStreamCSV` dumps a load profile to CSV.

**Three rules that will save you an evening:**

1. Set `Maze Name` before the first build, as soon as there are two mazes.
2. Place transition points only with `Paint Band = Play`.
3. The manifest on the character is a manual link. Rebuild a maze under a different name and you have to carry it over yourself.

---

## 16. Generating a maze

Drawing every corridor by hand is one way; the other is a generator. It fills the same grid, so everything after it — rooms, objects, `Apply Changes` — is the same.

1. In the mode panel, **Maze Features → Generator**: pick `Classic`.
2. On a 50 cm grid start with `Corridor Width 6`, `Corridor Height 6` and `Agent Height Cells 4` — the stock character is 176 cm.
3. **Generate Maze**. With `Regenerate On Change` on, every committed change of a setting generates again: move a slider, let go, look.
4. `Style`: `Long Corridors` for long winding corridors, `Branchy` for many short dead ends, `Path First` for a main path with a set number of branches.
5. Shape it: `Dead End Chance` and `Dead End Shapes` for pointed, slanted and round ends; `Erosion Amount` for rough walls and ceilings.
6. Not happy with the layout? **Reroll** for a new seed — or set the targets (`Target Dead Ends`, `Target Min Confusion`…) and press **Find Seed**.

**Check:** the maze fills the grid, the corridors are taller than the character, and **Analyze Paths** draws a yellow main path from the start to the exit.

**Planning the path by hand** (`Style = Path First`): `Tool = Path`. Click the points of the main path; `Ctrl` + `Shift` + click starts a branch, `Shift` + click adds points to it, `Ctrl` + click removes the nearest one. The generator runs the path through your points. **Clear Path Plan** forgets them.

---

## 17. Reachability, doors and keys

1. **Check Reachability.** Rooms nobody can reach are hatched red, rooms reached in part orange.
2. **Fix Reachability** joins them with tunnels and shafts. With `Require Ladders` on it also stands ladders in the shafts that have to be climbed (`Fix Ladder Type`).
3. Doors and keys are two library types: `Lock Role = Door` with a `Maze Lock` component on its blueprint, `Lock Role = Key` with `Maze Key`. Put a `Maze Keyring` component on the character.
4. **Generate Locks** places `Lock Count` doors, each with its key in another part of the maze. Or place them with the brush, choosing the colour in `Paint Lock Id`.
5. **Check Reachability** again: it plays the doors out and frames in red a key that cannot be had or a door that never opens.

**Check:** in PIE the key is picked up on touch and the door of its colour opens. Leave the room and come back — the door is still open.

---

## 18. Ladders

### 18.1 The ladder type

In the library, a type for the ladder and one for its platform:

| Field | Ladder | Platform |
|---|---|---|
| `Category` | `Climb` | `Climb` |
| `Actor Class` | your ladder blueprint | your platform blueprint |
| `Allowed Anchors` | `Floor` | `Free` |
| `Top Piece Type` | the platform's `Type Id` | empty |
| `Climb Height Property` | e.g. `NumberOfRungs` | empty |
| `Climb Unit Cm` | the height of one section | `0` |

If the blueprint builds itself out of sections in its construction script — `NumberOfRungs` sections of `Ladder Section Mesh Hight` each — then `Climb Unit Cm` is the height of a section, not the rung spacing: a 50 cm section with four rungs is `50`. If the blueprint also adds a top or bottom piece of its own, either leave `Top Piece Type` empty or turn that piece off, and take its height off with `Climb Extra Units`.

### 18.2 Standing them

`Tool = Ladder`, or just click the ladder's swatch in the palette.

- **First click — the bottom:** on the floor of the shaft, or on the platform of the ladder below.
- **Second click — the top:** at the floor of the exit the ladder leads to.

One ladder the whole way, its platform on top. A shaft with four exits is four pairs of clicks: each ladder stands on the platform of the one before. The status line says where the bottom and the top will snap and how many rungs the ladder will get; `Shift` + click cancels a started ladder, or removes the ladders in a column.

For a generated maze where every hole is meant to be climbed, **Place All Ladders** does it in one press; the Classic generator does it itself if its `Ladder Type` is set.

### 18.3 Save them

**Save Ladder Map**, then save the spawn asset. After `Clear All Ladders`, a regeneration or a slip, **Load Ladder Map** brings every ladder back.

### 18.4 Out of the player's way

A ladder stands in the player's plane and blocks his path. Give the ladder type and the platform type `Offset Y = -60…-80` (towards the background), and make sure the ladder's trigger boxes still reach the player's plane.

**Check:** after `Apply Changes` each ladder stands on its floor and reaches its platform, and the log says `climbs N cm — NumberOfRungs = M` for each one.

---

## 19. Moving a maze to another project

Everything you made is in assets: the maze asset (cells, rooms and merges, the generator and the path plan), its spawn asset (objects, ladders, the ladder map), the object library and the build settings.

1. Copy `Plugins/MazeForge` into the new project — the same engine version — and build it.
2. In the old project: save everything; press **Save Ladder Map** for safety.
3. Content Browser → right-click the maze asset → **Asset Actions → Migrate…** → the new project's `Content` folder. In the list, check that the spawn asset, the library, the build settings and the blueprints are ticked: the first three are soft references.
4. The generated room levels and meshes need not go: **Apply Changes** builds them again. Take them along only if you placed decor inside the room levels (`Bind Decor To Rooms`).
5. In the new project: the mode → `Target Asset` → **Apply Changes**.

**Check:** the rooms, objects and ladders stand where they stood, with the same ids.

---

## 20. Floors

The ground the player runs on is `Floor` cells; the rest of the mass is `Solid`. The two are the same mass with the same collision — the difference is the material: `Floor` has its own entry in the palette.

### 20.1 Give the floor a material

`DA_MazeBuildSettings → Palette → Floor`: set `Material Override`, or a few `Variants`. Without it the floor builds exactly like the walls.

### 20.2 Where floors come from

- **After generation**, in any generator — Mesh, Classic, Image, Random, Dungeon: `Mark Floor Surfaces` (on) makes every reachable horizontal surface `Floor`, `Floor Surface Cells` deep (1).
- **After each brush stroke**, with `Brush → Auto Floor Surfaces` on: cut a hole or draw a step and the top it laid bare becomes `Floor`; bury a floor and it becomes `Solid`. Part of the stroke's `Ctrl+Z`.
- **On the button** `Brush → Mark Floor Surfaces` — for a maze made before this existed, or drawn by hand.

Only surfaces the player can get onto count — the air above them has to be reachable, as `Check Reachability` sees it without gravity. A window sill, a pocket sealed in the mass, a slit he does not fit in stay `Solid`. The reach starts at the `Entry` points; with none, at the lowest place to stand. The edges of the map count too: the bottom border where he walks on it, and roofs open to the sky when `Grid → Borders → Close Top` is off.

### 20.3 An existing maze

1. `Target Asset` — the maze.
2. `Snapshot → Save`, for safety: retyped cells go back to surface variant 0.
3. **Mark Floor Surfaces**. The log says `floor surfaces marked — N Floor cell(s), M cell(s) retyped`. Works on a built volume as well; no need for `Change Current Maze`.
4. **Apply Changes** — the rooms rebuild with the floor material.

**Check:** the floors in the preview are the `Floor` colour, window sills and sealed pockets are not. If most floors stayed `Solid`, the reach started in a cut-off piece: put an `Entry` point where the player arrives and press the button again.

---

## 21. Hewn rock: the Rough brush

Cells give ruler-straight rock. The `Rough` brush marks where it should look hewn — shafts and tunnels cut by hand — and the bake chips those faces.

1. `Tool = Rough`. `Depth Apply = Fill Bands` covers the whole depth; for the face the camera looks at, `Play Band Only` is enough.
2. Paint over the edges of the tunnels and shafts — the mass and the air in front of it alike. The painted area is hatched violet.
3. **Apply Changes**. The faces there come out in flat chips with sharp edges, mostly cut into the rock, a little jutting out.
4. Too strong, too fine? `Build Settings → Rough`: `Rough Amplitude Cm` (depth), `Rough Chip Size Cm` (size), `Rough Tilt` (slant), `Rough Seed` (another pattern) — and **Apply Changes** again. Staircases of cubes are smoothed into slopes before chipping (`Rough Smooth Radius Cells`, `0` — off); `Rough Smooth XZ Only` keeps the edge into the depth sharp.

Collision stays the cells: the player runs exactly as before. The walkway — the top of `Floor` cells — stays flat, and so does the back wall. Paint at least a few cells: at the edge of the mask the chips fade to the straight face. `Shift` + click erases; **Clear Rough Mask** removes it all.

**Check:** after `Apply Changes` the painted edges are chipped, the rest straight, and there is no gap where the two meet.

---

## 22. Lifts

A lift is a platform in a shaft with two stops and a call terminal at each: press the button and the platform rides to you (or is already here); step on it and after `Board Delay` (0.75 s) it takes you to the other stop. Without the button it goes nowhere: **button + step on = ride**. A terminal finds its lift on its own or by name (`Lift Id`), the way a key finds its door. The lift crushes whoever is under it, and a fall down an empty shaft hurts or kills depending on the height.

What it takes: a shaft, two Blueprints, two library types, and a component on the character.

### 22.1 The shaft

The shaft is **one room** from its floor to the upper exit (the `Rooms` tool, as in 2.3). A floor at the bottom, an exit at the top: a ledge to the side or a hole in the slab. The terminals go on the landings by the stops — those may be in neighbouring rooms.

### 22.2 The platform Blueprint

1. **Blueprint Class → Actor**, name it `BP_LiftPlatform`.
2. The root is the platform's `Static Mesh`: **as wide as the shaft**, **up to 30 cm** thick (otherwise the character cannot step onto it — at a stop the platform stands proud of the floor by its thickness).
3. On the root: `Mobility = Movable`, `Collision Presets = BlockAll`.
4. **Add Component → Maze Lift.** The fields can stay as they are: the stops, the name and the starting stop come from the export. `Speed` is in cm/s; raise it for a tall shaft (a 57 m shaft at 200 cm/s is almost 30 seconds of riding). Leave the Blueprint's `Lift Id` field **empty**: it is the class default, and with it every lift in the game shares one name. `Ride When Boarded` (on) — after a terminal's call, a player who steps on and stays `Board Delay` is taken to the other stop. Arrived — press the terminal again to ride back. `Ready` — called and waiting for a rider (for a lamp on the terminal). The platform needs no Box Collision.
5. Optionally — sound and lights on `On Departed` / `On Arrived`.

### 22.3 The terminal Blueprint

1. **Blueprint Class → Actor**, `BP_LiftTerminal`: the panel mesh. Then the zone where the player can press: in the **Components** panel select the root (the mesh) → **+ Add → Box Collision**. Set its `Box Extent` so the box covers the spot **in front of** the panel where the player stands — say `(60, 100, 100)`; along Y (depth) it must reach the player's plane even if the panel is pushed towards the background. `Collision Presets = OverlapAllDynamic`, `Generate Overlap Events` on. Without this box neither the touch nor `Get Overlapping Actors` works: a `BlockAll` mesh stops the player rather than overlapping them.
2. **Add Component → Maze Lift Call.** Leave the fields — the export writes which lift to call and to which stop.
3. Optionally: `On Called` — a green lamp, `On Refused` — a red blink.

### 22.4 The player's button

In the character Blueprint, on your interact input (say `E`):

`Get Overlapping Actors` (Class Filter — `BP_LiftTerminal`) → `For Each Loop` → `Get Component by Class` (`Maze Lift Call`) → `Is Valid` → **`Call`**.

For a first try without a button: turn on `Call On Touch` on the terminal — the lift comes as soon as the player touches the zone.

### 22.5 Library types

In `DA_MazeObjectLibrary → Types` — two entries:

| Field | Platform | Terminal |
|---|---|---|
| `Type Id` | `LiftPlatform` | `LiftCall` |
| `Category` | `Climb` | `System` (any) |
| `Actor Class` | `BP_LiftPlatform` | `BP_LiftTerminal` |
| `Editor Color` | something visible, say orange | the same |
| `Allowed Anchors` | `Floor` | `Floor` |
| `Footprint Cells` | `(shaft width, 1)` | `(1, 2)` |
| **`Lift Role`** | **`Platform`** | **`Call`** |

The rest as default. Leave the platform's `Climb Height Property` and `Top Piece Type` empty.

### 22.6 Placing the lift

1. `Target Asset` — your maze; in the `Objects` palette click the `LiftPlatform` swatch — `Tool = Lift` switches on.
2. Want the platform to wait upstairs? Turn on `Brush → Lift Starts At Top`.
3. **First click** — in the **left** column of the shaft, near the floor: the lower stop. Green shows where it goes (the shaft floor or the nearest exit).
4. **Second click** — at the level of the upper exit: the stop snaps to the exit like a ladder top. The highlight: the shaft green, the lower stop amber.
5. The status line says which click is next. `Shift` + click — cancel what was started, or remove the lift in that column. A new lift in the same column replaces the old one.
6. Done: the map shows a thin shaft frame and a crossed frame where the platform waits. The log: `Lift: 'LiftPlatform' at X …, stops Z … and Z … (… cm), waits at the lower one.`

### 22.7 Placing the terminals

1. `Tool = Objects`, the `LiftCall` swatch.
2. One terminal on the floor **beside** the shaft at the lower stop, the other on the landing by the upper exit. Not in the shaft itself: the platform comes down there.
3. The terminal's level (the cell it stands on) must be close to the stop's level: ±1.5 m of give (±3 cells at 50 cm a cell). A landing is seldom dead level with the stop — that is fine.

**Several lifts side by side.** Before placing a lift set `Brush → Paint Lift Id` (say `EastLift`), place the lift and both its terminals, then change the name for the next one. An empty name — on export the terminal takes the nearest lift along X with a stop at its level.

### 22.8 Build

1. **Check Reachability** — the lift counts as a climb, the upper exit is no longer red.
2. `Build → Apply Changes`. For every terminal the log says what it is bound to: `lift terminal 'LiftCall' at X … Z … calls lift 'Lift_225_201' to its lower stop` (and `upper` for the other one). No lift found — `lift terminal … finds no lift` (a warning): move the terminal closer to the stop's level, or give it and the lift the same `Paint Lift Id`.
3. Save the spawn asset.

### 22.9 Hazards

**The lift crushes.** The platform rides down onto a pawn — the pawn gets `Apply Damage` with `Crush Damage` (10000 by default, i.e. death), and the lift sends `On Crushed(Victim)`. Someone standing beside it (`Crush Inset`) or riding on it is not caught. Off — the `Crushes` tick on `Maze Lift`; `Crush Damage = 0` — the event only, the Blueprint decides.

**Falling.** On the character — **Add Component → Maze Fall Damage**:

| Fall height | What happens |
|---|---|
| up to `Safe Height` (350 cm) | nothing |
| `Safe Height` to `Lethal Height` | damage from `Min Damage` (10) to `Max Damage` (80), linear |
| from `Lethal Height` (1000 cm) | `Lethal Damage` (10000) — death |

Height is measured from the top of the jump to the landing: a jump down a shaft, off a ledge, off a ladder — one rule. Landing on a lift platform is a landing too.

**Health.** Both kinds of damage go through `Apply Damage`. On the character: `Event AnyDamage` → `Health -= Damage` → if `Health <= 0` — death. `On Fall(Height, Damage, bLethal)` is for the animation, sound, camera shake.

### 22.10 Checking in game

1. PIE. The platform waits below. Step on it — it goes nowhere.
2. Step off, press the lower terminal (log: `already there, ready to ride`), step on — after 0.75 s it rides up.
3. Upstairs step off, press the upper terminal, step on — it rides down.
4. Walk upstairs and press the upper terminal while the platform is below — it rises to you; step on — it rides down.
3. Stand under the platform and call it down — `On Crushed`, death.
4. Jump into an empty shaft higher than `Lethal Height` — death; from just over `Safe Height` — a little damage.
5. Leave the room and come back — the lift is at the stop where you left it.

### 22.11 When it does not work

| What you see | Why | What to do |
|---|---|---|
| The terminal does nothing | Look at `LogMazeForge` at the moment of the press — one of the lines below is there | |
| `Lift call …: no lift '…' is loaded` | The shaft's room is not loaded, or the terminal has another `Lift Id`; `None` — the export bound it to nothing | Streaming of neighbouring rooms; `Lift Id`; terminal at a stop's level and `Apply Changes` |
| Stepped on the platform — it does not go | The terminal at this stop was not pressed | Button first, then step on |
| Touching / pressing the terminal does nothing, the log is empty | No Box Collision, or it does not overlap the player | 22.3: a Box Collision with `OverlapAllDynamic` reaching the player's plane |
| `Lift '…': called to stop N — already there, ready to ride` | The lift is here and waiting | Step on the platform — it goes |
| `Lift '…': one stop only` | The platform was placed with the `Objects` brush, not the `Lift` tool | Remove it and place it with two `Lift` clicks |
| `Lift '…': the root component is not Movable` | The Blueprint's root is `Static` | `Mobility = Movable` |
| `Lift call …: refused stop N` | The lift is riding (`Ignore Calls While Moving`) | Wait for it to stop |
| A terminal calls the wrong lift | Two lifts side by side without names | `Paint Lift Id` on the lift and its terminals |
| Log: `two lifts are called …` | Two lifts share a `Lift Id` | Different names |
| Terminals call the wrong lift, log `two lifts are called …` | `Lift Id` typed into the Blueprint's component — one name for every lift | Clear `Lift Id` in the Blueprints' `Maze Lift` and `Maze Lift Call` |
| Nothing in this section works, no `Lift` lines in the log | The editor runs an old build of the plugin | Close Unreal, build from the IDE (not Live Coding — headers changed), `Apply Changes` |
| The player falls through / is not carried | The root is not `Movable`, or has no blocking collision | `Mobility = Movable`, `BlockAll` |
| The character cannot step onto the platform | The platform is thicker than the character's step | Under 30 cm, or a larger `Max Step Height` |
| The platform is the wrong width / sticks into the wall | `Footprint Cells.X` ≠ shaft width, or the first click was not in the left column | Fix the footprint, place it again |
| The platform swatch switches on `Ladder`, not `Lift` | `Lift Role` is not `Platform` | Set `Lift Role = Platform` |
