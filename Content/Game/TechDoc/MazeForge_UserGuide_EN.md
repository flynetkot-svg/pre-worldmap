# MazeForge — User Guide

**Who this is for:** you have the plugin and you want to start building levels.
**Version:** MazeForge 0.3.0 · Unreal Engine 5.8.2

> Russian version: `MazeForge_UserGuide_RU.md`.

This document takes you from "the plugin isn't in my project" to "I'm running through the maze and rooms are streaming in". The sections are in the order you go through them the first time. Skip anything you have already done.

For the architecture and implementation details behind all of this, see `MazeForge_TZ_AsBuilt_EN.md`.

---

## 1. What this is

A tool for 2.5D side-scrollers. You draw or generate a maze as a voxel grid, the plugin slices it into rooms, bakes the geometry, lays it out across separate levels, and streams those levels in around the player at runtime.

Three things to understand before you start, or everything else will look strange:

**Axes.** X runs along the level. Z is height. **Y is depth** — the camera axis. The camera sits at +Y and looks inward. You draw in the **Left** or **Right** viewport, or in perspective.

**Depth bands.** Depth is divided into three bands: **Background** — decoration behind the player, **Play** — the plane the player moves in, **Foreground** — decoration in front. Collision exists **only in Play**. World zero on Y is the center of Play.

**Plane and volume.** You always draw and generate in a **single plane**, one cell per column. Volume is grown by a separate button once you are happy with the maze. This isn't a limitation — it's what lets you edit a 707 × 376 map without the editor crawling.

---

## 2. Installation

1. Copy the `MazeForge` folder into `<Project>/Plugins/`.
2. Right-click the `.uproject` → **Generate Visual Studio project files**.
3. Build the project.
4. Launch the editor. Verify: **Edit → Plugins**, category **Level Design**, MazeForge is enabled.

The plugin contains C++ and has to be compiled. Dropping it into a project with no code will not work.

### Engine settings

Add this to `Config/DefaultEngine.ini`:

```ini
[SystemSettings]
s.ForceGCAfterLevelStreamedOut=0
s.ContinuouslyIncrementalGCWhileLevelsPendingPurge=0
gc.TimeBetweenPurgingPendingKillObjects=120
s.AdaptiveAddToWorld.Enabled=1
```

The first two lines are mandatory. Without them the engine runs a **full blocking garbage collection every time a room streams out**, and the character stutters as you run. That's not a quirk of the plugin — it's the engine's default behavior.

The third line follows from the first two: garbage now accumulates for longer. If `Mem` in `stat unit` creeps up during a long run and never comes back down, lower it to 60.

---

## 3. What to create

Four assets to build a maze, and two more once you get to placing objects. Almost all of them are created once and never touched again.

### 3.1 The maze

**Content Browser → right-click → MazeForge → Maze Grid.**

This is your level. Give it a meaningful name: `U_MyMaze`.

Open it. Configure it **once**:

| Field | What it sets |
|---|---|
| `Grid → Cell Size` | cell size in units. The standard UE cube is 100 |
| `Grid → Size XZ` | map dimensions in cells |
| `Grid → Depth` | depth profile: Background / Play / Foreground |
| `Grid → Borders` | whether to wall off the edges of the world |
| `Generation → Generator` | what fills the grid |
| `Slicing → Slicer` | what slices it into rooms |
| `Build → Maze Name` | short name of this maze. Leave empty unless you have more than one — see below |
| `Build → Build Settings` | reference to the build settings, see below |
| `Build → Spawns` | reference to the spawn asset. Empty means this maze has no objects, see 3.6 |

There are deliberately no buttons in this asset. It stores parameters; the work happens in the editor mode.

**About `Maze Name`.** Empty means the output goes where it always went, byte for byte, so an existing maze keeps its assets. Fill it in — `Prod`, `Test` — and everything this maze produces moves into a compartment of its own:

```
/Game/MazeForge/Maps/Prod/L_Prod_R_000_000
/Game/MazeForge/Meshes/Prod/SM_Prod_R_000_000_Play
/Game/MazeForge/Maps/Prod/DA_MazeWorldManifest_Prod
Outliner:  Levels/Prod/L_Prod_R_000_000/MazeMeshes
```

**Set it as soon as you have a second maze.** Room ids are not unique between mazes — every slicing starts at `R_000_000` — so two mazes sharing one set of build settings write the same level names, the same mesh names and the same manifest. Building the test maze overwrites the production one, and nothing says so.

Anything that is not a letter, a digit or an underscore is stripped: the name becomes part of a package path.

Migrating an existing maze is one step: set the name, press `Apply Changes`. The old assets stay on disk as orphans — delete them by hand once.

**About depth.** For a side-scroller, 3 / 2 / 2 is usually enough. More volume means more room for decoration, but also more geometry: columns × depth = cells. At 95 thousand columns and a depth of 22 you get 2 million cells, and that's already heavy. The plugin does the math and warns you in the log.

### 3.2 Build settings

**Content Browser → right-click → Miscellaneous → Data Asset → Maze Build Settings.**

Name it `DA_MazeBuildSettings`. Then **make sure** you point the maze at it: `U_MyMaze → Build → Build Settings`.

> While that field is empty, the asset isn't used at all. Export silently falls back to hardcoded defaults, and your edits in the asset go nowhere. This is the single most common mistake when starting out.

What's inside:

**Output → Packages** — where assets are written. Paths are relative to the content root and start with `/Game`.

| Field | Default |
|---|---|
| `Level Package Root` | `/Game/MazeForge/Maps` |
| `Mesh Package Root` | `/Game/MazeForge/Meshes` |
| `Manifest Package Root` | empty — next to the levels |
| `Manifest Asset Name` | `DA_MazeWorldManifest` |

**Output → Outliner** — folders for the generated actors.

| Field | Default |
|---|---|
| `Outliner Root Folder` | `Levels` |
| `Outliner Mesh Sub Folder` | `MazeMeshes` |
| `Outliner Folder Per Room` | enabled |

The layout exists for one reason: a single eye toggle in the Outliner hides all of a room's generated geometry while your decoration and anchor stay visible.

**Output** — everything else.

| Field | What it means |
|---|---|
| `Generated Tag` | tag applied to generated actors. A re-export deletes **only tagged actors** and leaves your decoration alone |
| `Rooms Per Flush` | how many rooms to process before freeing memory. **Do not set it to 0** on large mazes |

**Bake** — how the geometry is assembled.

| Field | What it means |
|---|---|
| `bSplitByDepthBand` | three meshes per room instead of one. Lets you hide the foreground separately |
| `bCullEnclosedCells` | discard cells that are surrounded by solid. The main geometry saving |
| `bCullFarBoundaryFaces` | don't build the far face along −Y. It isn't visible in game, but the mesh ends up non-watertight |

**Palette** — what each cell type is drawn with, and this is where surface variants live.

Each entry has a base mesh, a base material and an editor colour, plus a list of **Variants**:

| Field of a variant | What it means |
|---|---|
| `Name` | what it is called in the brush tooltip and in the "Painting:" line — `Brick`, `Rock`, `Metal` |
| `Material` | the material for this variant. Empty means the base material of the type |
| `Editor Color` | the swatch colour, and the colour of the cells in the preview |

Out of the box `Solid` ships with `Brick / Stone / Building` and `BackWall` with `Brick / Rock / Metal`, with colours set and materials left empty for you to fill in.

A variant is **not** a new cell type. The geometry is identical; only the surface differs. One stretch of a level being brick and another rock is exactly what the variant index is for — it is not a reason to invent a cell type per material.

A room whose cells use two variants bakes into **one mesh with two material slots**, not two meshes.

### 3.3 Streaming rules

**Content Browser → right-click → Miscellaneous → Data Asset → Maze Streaming Rules Asset.**

Name it `DA_MazeStreamRules`. Configured once, hooked up to the character (§7).

Inside are the rule list and the budget:

| Field | Default | What it means |
|---|---|---|
| `Load Threshold` | 0.35 | above this — start loading |
| `Unload Threshold` | 0.15 | below this — unload |
| `Max Loaded Rooms` | 9 | how many rooms are kept resident at once |
| `Max Concurrent Loads` | 2 | how many loads are in flight |
| `Min Time Loaded Seconds` | 2.0 | minimum lifetime of a loaded room |

The two separate thresholds give you hysteresis. With a single one, a room sitting on the boundary would load and unload every frame.

Add rules to the `Rules` array. At minimum, **Camera Frame** and **Player Radius**. The rest as you like: **Portal Graph** (neighbors by traversability rather than straight-line distance), **Velocity Predict** (leads the player's velocity), **Vertical Motion** (falling down a shaft).

### 3.4 Style preset (optional)

**Content Browser → right-click → Miscellaneous → Data Asset → Maze Editor Style Asset.**

Colors and line widths in the editor. If you don't create one, the built-in values are used. Hooked up in the mode panel, field `Display → Style`.

Object markers are set here too: `Objects → Object Marker` — the thickness of the frame and cross that placed objects are drawn with on the 2D map, plus a visibility tick to take them out of sight in one click. The colour comes from the object type, not from here.

### 3.5 The object library

**Content Browser → right-click → Miscellaneous → Data Asset → Maze Object Library.**

The catalogue of what can be placed at all. One per project: a lamp is the same lamp in every maze, and describing it again per maze is the mistake already avoided with the surface palette.

Each entry is one kind of object:

| Field | What it sets |
|---|---|
| `Type Id` | internal name; placements refer to the type by it. Unique, and not to be changed once anything is placed |
| `Display Name` | what it is called in the palette |
| `Category` | Climb / Decor / Item / Enemy / System — groups the palette |
| `Actor Class` | what to spawn. A blueprint or a C++ actor class |
| `Editor Color` | the colour it is drawn in on the 2D map |
| `Allowed Anchors` | what it can hold on by: Floor, Ceiling, Wall, Free. More than one may be ticked |
| `Footprint Cells` | how much room it needs: X along the level, Y up Z |
| `Facing Mode` | Fixed — always the set angle; Random X — left or right by the seed; Away From Wall — facing away from what it leans on |
| `Fixed Rotation` | the base angle |
| `Offset` | a nudge inside the cell, in units. A lamp hanging a little below the ceiling |

**About `Type Id`.** It is a key, not a label. Placements refer to it; rename it and every placement of that type is orphaned, and the export reports them as `unknown type`. Change the label in `Display Name` instead — that is what the palette shows, and the row names both anyway: `Placing: Box (Floor)`.

**About `Allowed Anchors`.** The anchor is what the object touches the maze's mass with: Floor by its bottom, Ceiling by its top, Wall by a side, Free by nothing. Several may be allowed, and the brush takes the first that fits, in the order Floor → Ceiling → Wall → Free. A crate needs `Floor`; a torch `Wall`; a lamp `Ceiling`; a flying enemy `Free`.

Careful with `Free`: it fits anywhere there is space, and therefore **turns the check off**. It also puts the object in the middle of the cell rather than on an edge — so the same crate ends up half a cell higher than its neighbours on the floor.

**About `Footprint Cells`.** This is the space the object needs **clear**, not the size of the mesh. A two-by-three wardrobe is `(2, 3)`, and all six cells must be free or it will not fit.

### 3.6 The spawn asset

**Content Browser → right-click → Miscellaneous → Data Asset → Maze Spawn Asset.**

Where **this** maze's objects stand. One per maze, referenced from `U_MyMaze → Build → Spawns`.

| Field | What it sets |
|---|---|
| `Library` | which library describes the types the placements refer to |
| `Placements` | the placements themselves. Read-only — the brush fills them in |
| `Next Id` | the next number to hand out. Read-only |

A separate asset from the grid, and the reason is lifetime rather than tidiness. The maze gets generated, cleared, restored and redrawn dozens of times; the decor pass is later and far more expensive work. The practical consequence: **`Clear All Changes` erases the drawing and leaves the objects alone.** They have a button of their own.

And the reverse consequence: **the snapshot (`Save` / `Restore`) holds the drawing only.** Objects are not in it, and `Restore` will not bring them back.

---

## 4. Editor mode

Viewport top bar → the **Modes** dropdown → **MazeForge**.

A panel appears on the left. It reads top to bottom the way the job goes:

| Group | What it is for |
|---|---|
| **Target** | which maze you are editing, and its state in one line |
| **Generate** | fill the grid from the target's generator |
| **Brush** | everything you draw with |
| **Objects** | the palette of objects and what has been done with them |
| **Display** | what the viewport shows |
| **Snapshot** | keep a state worth coming back to |
| **Build** | turn the drawing into levels — or throw it away |
| **Edit** | the loop for a maze that already exists |
| **Advanced** | the pipeline one step at a time. Folded away |

Two of the groups switch themselves off when they make no sense, and say why rather than just going grey. See **Generate** and **Edit** below.

### Settings

**Target** — which maze you are editing. Pick `U_MyMaze` here, or the mode draws nothing.

Switching the target **takes the previous maze off the map**: its room levels are detached and the Outliner folders it created are deleted. The new maze's levels are *not* attached — press `Apply Changes` when you want them there. Detaching is instant; attaching loads every room level, and that does not belong on a dropdown.

So switching between two mazes is: pick the other one in `Target Asset`, then `Apply Changes`.

Next to it is the **Status** line — the state of the target in one line:

```
plane · cells 28622 · rooms 208 · meshes: ready · snapshot: 28622 cells, 707x376, ...
```

It answers most "why isn't this working" questions without opening the asset.

**Generate**

One button, `Generate Maze`, which runs the generator set on the target asset (`U_MyMaze → Generation → Generator`). Under it a line says what that generator is:

```
Generator: Random
```

With the **Manual** generator the button is disabled, and the line says so:

```
Current mode: manual drawing — there is nothing to generate.
```

That is not a fault. Manual means the grid is yours and the plugin has nothing to fill it with.

**Brush**

| Field | What it means |
|---|---|
| `Tool` | what the mouse does: **Cells** — draw the mass of the maze, **Objects** — place objects |
| `Paint Type` | what you place: **Solid**, **Floor**, **BackWall** |
| `Paint Variant` | which surface variant of that type. Set it by clicking a swatch, see below |
| `Brush Size` | brush size in cells |
| `Depth Apply` | how a stroke lands across depth: `Fill Bands` by the profile flags (usually Background + Play), `Active Slice Only` the current slice only, `All Depth` the full depth, `Play Band Only` the play band only |
| `Active Depth Slice` | the active slice — the plane the cursor snaps to |
| `Paint Object Type` | which object you are placing. Set by clicking a swatch in the **Objects** group |
| `Paint Band` | which depth band the object goes into. **Play** — the same band as the player, and the only one with collision. Background and Foreground are decor you walk through |

The fields switch with `Tool`: in Cells mode the object fields are hidden, in Objects mode the cell fields are.

Below the fields is the **swatch palette** — the variants of the type currently selected, straight from `Palette → Variants` in the build settings. Click a colour and you are painting with it; the selected swatch gets a white frame. Under the swatches is a line saying, in words, what is about to be painted:

```
Painting: BackWall — Rock
```

The line is there because a wall of a dozen greys tells you what the colour is and not what the material is called. It resolves the name afresh every frame, so renaming a variant in the build settings updates it immediately.

If the selected type has no variants, the row says so instead and tells you where to add them.

`Fill Back Wall` and `Clear Back Wall` are in this group too: they are the brush applied to the whole map at once. See **Back walls and windows** below.

**Objects**

The library's types as swatches, coloured from `Editor Color`. Click one and that is what you place. Under the swatches, a line:

```
Placing: Box (Box1)   |   placed 3, of them never exported 0
```

In brackets is the `Type Id`, because that is what the log speaks, and the two names side by side save you asking which object a warning is about.

`placed` is how many are down in total. `never exported` is how many of those have never reached a level: numbers are handed out at spawn time, so this is an honest "drawn but not built". After a successful `Apply Changes` the second number should be zero; if it is not, the export skipped something and said why.

`Clear All Objects` removes **every** placement of this maze. Its own button, and deliberately not part of `Clear All Changes`: the drawing is redrawn often, the decor is placed once and slowly. The snapshot does not hold objects, which makes this the one irreversible button in the panel.

**Display**

| Field | What it means |
|---|---|
| `Style` | style preset |
| `Show Grid` / `Show Rooms` / `Show Preview` | what gets drawn |
| `Show Room Preview` | draw the slicing that *would* happen instead of the one that was stored |
| `Isolate Active Layer` | keep the layer being painted where it is and push the rest back, dimmed. On by default |
| `Peek Key` | hold it to suspend the isolation. `Q` by default |
| `Inactive Layer Dim` | how strongly the layers you are not painting are held back. 1 — not at all |
| `Grid Window Cells` | fallback size of the grid window |

**Show Room Preview** cuts nothing and writes nothing. The room grid is computed from the slicer's settings, drawn, and thrown away. Use it to see where the seams will fall before there is any slicing at all — and afterwards, to see the effect of changing `Room Size` without re-slicing and invalidating every baked mesh. Only one set of frames is ever drawn: with this on you see the prospective slicing, with it off the stored one.

**Isolate Active Layer** is what makes a back wall in a deep niche paintable at all. Without it you are drawing blind: the maze mass stands between the camera and the wall, and the brush lands on cells nobody can see. With it on, select `BackWall` and the maze fades back; select `Solid` and the maze comes forward again.

In the Left and Right orthographic views — the ones you draw in — this costs nothing in accuracy: the projection runs along Y, so moving a layer in depth changes what covers what and not where anything appears to be. In a perspective viewport the shifted layers do slide a little; turn it off if that gets in the way.

**Peek — hold `Q`.** Isolation is also what hides everything the layer you are painting now stands in front of: cover a large stretch with back wall and the floors that were there are gone from view. Hold the peek key and the layers drop back into their true depth order for as long as you hold it — the wall goes behind, the floors are visible. Release and you are painting again, with the same type and the same variant still selected. The overlay shows `PEEK` while it is held, because what you see then is not the layer order a stroke would land in.

There is no opacity slider, and that is a limit rather than an omission. The preview is drawn with the engine's opaque `BasicShapeMaterial`; blend mode is a static property of a material, so a dynamic instance cannot turn it translucent — real see-through would mean shipping a material with the plugin. It would also look poor: after `Build Depth Volume` a column holds dozens of identical cubes, and translucent surfaces overlapping that heavily sort badly against each other.

**Inactive Layer Dim** is the closest thing to that opacity slider. It blends the held-back layers toward a dark grey rather than multiplying them toward black — which is why a dark back wall stays visible at all. Turn it up if the maze reads as noise, down if you lose the layer you are drawing.

All of this is remembered between editor sessions.

**Snapshot**

`Save` and `Restore`. The snapshot lives in the asset itself and survives an editor restart, unlike Undo. It is taken automatically before anything that destroys work. `Restore` **swaps** the grid and the snapshot — press it twice and you are back where you started.

It keeps the drawing and nothing else. Levels are not part of a snapshot.

**Build**

`Apply Changes` does the whole build in one press: depth volume, slicing, meshes, levels, and the levels into the map. It is the only button most first builds need.

`Clear All Changes` throws the maze away, and throws away everything that depended on it:

1. the room levels come out of the map;
2. the grid, the slicing and the bake state are erased;
3. the manifest reference is cleared.

A snapshot is taken first, so `Restore` brings the **drawing** back — not the levels; those come back from `Apply Changes`. The level and mesh assets stay on disk and are overwritten by the next build.

### Controls

| Action | Keys |
|---|---|
| Place a block | **LMB** |
| Erase | **Shift + LMB** |
| Rectangle fill | **Ctrl + LMB + drag** |
| Rectangle erase | **Ctrl + Shift + drag** |
| Place an object (`Tool` = Objects) | **LMB** |
| Remove an object | **Shift + LMB** |
| Active depth slice | **PgUp / PgDn** |
| Brush size | **[** and **]** |
| Peek — suspend the isolation | hold **Q** |

Rectangle fill is the big time-saver. Use it for walls, floors and shafts, then refine the details with the brush.

Objects go down **one click at a time, with no drag**, and that is deliberate: a brush you could drag would bury a room in crates faster than anyone could undo it. Erasing takes the topmost object in the cell — the one placed last.

The status line in the top-left corner of the viewport tells you where you are:

```
MazeForge  |  slice Y 0/21 (BACKGROUND)  |  brush 1  |  cells 28622  |  rooms 208  |  cursor  X 340  Z 118
```

If the viewport says **VIEW LOOKS ALONG THE DRAWING PLANE** — switch to Left, Right or perspective. Drawing in the Front or Top view is physically impossible: the cursor ray runs parallel to the plane.

---

## 4a. Changing a maze that is already built

Once the maze exists and its levels are in the map, you are no longer building — you are editing. That is a loop of its own, and it has two buttons in the **Edit** group at the bottom of the panel.

Both are disabled until a slicing exists, with a line saying why:

```
No maze yet — build one with Apply Changes first.
```

They take the room levels out of the map and put them back, and a drawing that has never been built has no levels to take out.

**Change Current Maze** — go from a finished maze to something you can draw on:

1. detaches the room levels from the map;
2. flattens the grid back to the plane (lossless — a column that had any mass keeps a cell, and the back wall stays in its own slice);
3. puts the active depth slice on the drawing plane, so the first stroke lands where you are looking.

**Apply Changes To Current Maze** — put it back. It does exactly what `Apply Changes` does; the two are separate buttons because they mean different things, and a single label would be wrong half the time. Step by step:

1. saves a snapshot of the plane (before the volume, because that is the state worth returning to);
2. builds the depth volume;
3. re-slices into rooms;
4. bakes **only the rooms that changed**;
5. exports the levels;
6. attaches them back to the map.

Then save everything — Ctrl+Shift+S.

Between the two buttons you draw exactly as you would on a new maze.

> Editing with the levels still attached is drawing blind: while a room's level is in the map the preview deliberately draws nothing for that room, because its real geometry is already on screen. That is what `Change Current Maze` detaches for.

---

## 4b. Placing objects

Crates, ladders, torches, enemies. The maze itself stays mass and nothing else; objects live separately and are built into levels by the same `Apply Changes`.

### What you need once

1. An object library (3.5) with at least one type in it.
2. A spawn asset (3.6) whose `Library` points at that library.
3. On the maze: `Build → Spawns` — that spawn asset.

If something is missing the panel says so outright: `No object selected — click a swatch.`

### How to place

1. `Brush → Tool` = **Objects**.
2. Click the swatch of the type you want, in the **Objects** group.
3. `Brush → Paint Band` — usually **Play**. It is the only band with collision.
4. LMB on a cell. Shift + LMB removes.

**Place into the empty cell next to the mass, not into the mass itself.** A crate stands *on* the floor, not *inside* it. The cell it goes into must be free; what holds it is what lies below (or above, or beside — depending on the anchor).

### The line in the frame tells you whether it will fit

Hover a cell and the overlay answers in advance:

```
MazeForge | slice Y 3/6 (PLAY) | ... | cursor X 12 Z 40 | Box1 fits here
MazeForge | slice Y 3/6 (PLAY) | ... | cursor X 12 Z 39 | Box1 WON'T FIT: X 12 Z 39 is mass
```

The reasons are named, and they are different:

| What it says | What to do |
|---|---|
| `X.. Z.. is mass` | you clicked into a floor or a wall. Take the empty cell next to it |
| `no mass in the row below` | there is nothing under the object. Put it on a floor, or allow the type another anchor |
| `no mass in the row above` | nothing to hang from |
| `no mass on either side` | no wall to lean on |
| `part of the footprint is outside the grid` | the object is bigger than one cell and hangs off the edge of the map |
| `the type allows no anchors at all` | the type has no `Allowed Anchors` ticked in the library |

**You can place it anyway.** The brush does not argue: the click works, a warning goes to the log, and the marker is drawn on the map. Sometimes a crate half sunk into a wall is exactly what you want. Just know what happens next.

### What `Apply Changes` does with them

Objects go into the room levels, into an Outliner sub-folder of their own (`MazeObjects` next to `MazeMeshes`), so one eye icon hides the walls and shows you where they ended up. The room is decided by the cell the object starts in.

The export re-checks every placement against the geometry as it stands now, and reports:

```
objects 12 (re-anchored 1, skipped 2, unknown type 0)
```

| Counter | What it means |
|---|---|
| `objects` | how many made it into levels |
| `re-anchored` | the support it was standing on is gone, but another one fits. Placed, rotation recomputed — check which way it now faces |
| `skipped` | **not placed**: nothing holds it. One log line each, with the type and the coordinates |
| `unknown type` | the type is gone from the library, or has no `Actor Class` set |

`skipped` are the ones you placed "anyway". The level stays clean, but the object disappears from the scene — which is why it is never skipped silently.

### Numbers

Every placement has a number, and it is handed out **at the moment the object actually reaches a level**. So the line in the panel reads literally: `never exported 3` means "three are drawn but have never been built".

The number travels into the level on a `Maze Object Id` component on the spawned actor — `PlacementId`, `TypeId`, `RoomId`. From a blueprint, read it with `Get Placement Id`. Numbers are never reused: delete an object and its number goes to nobody.

The export saves the spawn asset itself, along with the manifest.

> **Duplicating a maze.** Duplicating the grid asset does not duplicate the spawn asset — both mazes will point at the same one. Duplicate that too and the placements come across with their numbers, so the same number ends up on different objects. Harmless while nothing reads the numbers.

### What is not there yet

Objects are invisible to the checks: an object neither occupies its cell nor holds weight. So two objects fit in one cell, and a crate cannot be placed on a crate.

---

## 5. The pipeline, step by step

`Apply Changes` runs all of this in one press, and normally that is all you touch. The **Advanced** group holds the same steps as separate buttons — for when a build has gone wrong and you need to see which step it went wrong in, or to re-run one of them on its own.

The order below is the order they run in.

### Step 1 — get a maze

**Generate Maze** runs the generator configured in the asset. **Clear Maze** wipes the grid, taking a snapshot first.

The generator is chosen in `U_MyMaze → Generation → Generator`:

**Manual** — generates nothing; paint from scratch with the brush.

**Random** — a procedural maze in the spirit of Saboteur 2: BSP split into sectors, halls, floors, ladders, shafts, corridors.

| Parameter | Default | What it means |
|---|---|---|
| `Seed` | 1337 | one value — one and the same maze |
| `Min Sector Cells` | 22 | minimum sector of the split |
| `Floors Min` / `Max` | 1 / 4 | floors per hall |
| `Floor Gap Min` | 5 | minimum clearance between floors |
| `Ladder Chance` | 0.7 | chance of an opening in a slab (the hole a ladder needs) |
| `Shaft Count` | 6 | vertical shafts |
| `Corridor Height` | 4 | corridor height |
| **`Agent Height Cells`** | **2** | **player height in cells** |
| `bCarveIslands` | on | carve a path to cut-off regions |

`Agent Height Cells` is the most important parameter. It defines what counts as a passage: a corridor shorter than the player's height is a wall as far as the generator is concerned, whatever cell-level connectivity says. It is what guarantees you won't end up with unreachable rooms.

**Image** — a maze from a picture.

| Parameter | What it means |
|---|---|
| `Texture` / `Source File` | a texture in the project or a file on disk |
| `Target Size XZ` | size in cells. **0 — stretch to the whole grid** |
| `Color Rules` | color → cell type, with a tolerance |
| `Fallback Type` | what to place for colors that match no rule |
| `bFlipVertical` | images have their origin at the top, the grid has it at the bottom |

Defaults: black → Solid, white → Empty. The image is resampled by majority vote, so downscaling doesn't eat thin walls.

**Mesh** — reconstruction from a reference mesh or OBJ. For the "I already have the maze as geometry" case.

| Parameter | Default | What it means |
|---|---|---|
| `Mesh` | — | source `UStaticMesh` |
| `Voxel Size` | (50, 50, 50) | size of the cube the original is built from |
| `bFill Unreachable` | on | fill cavities the player can't get into |
| `Agent Height Cells` | 4 | player height for the reachability test |

Grid dimensions are taken from the mesh automatically. Check the log: it prints the source triangle count and the resulting grid size. If the triangle count looks suspiciously low, the mesh has Nanite enabled and a decimated fallback LOD is being read — the plugin warns you about this.

### Step 2 — finish it by hand

How the work goes:

1. **You draw in the plane.** Straight after generation the maze is flat — one cell per column. Fix it up with the brush as much as you need.
2. **Save Snapshot** — once you like the result.
3. **Build Depth Volume** — grows the volume according to the depth profile.
4. If you need to edit again — **Flatten To Plane**, edit, then rebuild the volume.

**Flatten To Plane** loses nothing: a column that had at least one solid cell stays a cell in the plane.

**Save / Restore Snapshot.** The snapshot lives in the asset itself and survives an editor restart, unlike Undo. It is taken automatically before `Clear Maze` and before generating with clearing enabled. `Restore Snapshot` **swaps** the grid and the snapshot — if you restored by mistake, pressing it again puts things back.

> Paint **in the plane**, not in the volume. In the volume every stroke touches the full depth, and on a large map that is noticeably slower.

**Back walls and windows.**

The back wall is the surface that closes the maze off from behind. It gives geometry but is not solid: it lives in its own slice at the far edge of the depth, the player never reaches it, and it takes no part in passability or collision.

Select `Paint Type = BackWall`, pick a variant from the swatches, and draw. You are drawing on a layer of its own — a stroke there never touches the maze, and erasing the maze never erases the wall behind it. Painting the back wall over the whole maze bounds at once is what `Fill Back Wall` is for; `Clear Back Wall` removes it and leaves everything else alone.

A **window is simply a cell where the back wall was not painted**. There is no window brush and there does not need to be one: through the hole you see the sky and the skybox. Erase with `Shift + LMB` while `BackWall` is selected and you have cut a window.

A room can consist of nothing but a back wall — the roof of a building with antennae on the backdrop, or a distant view of the same building. That is a valid room and slicing keeps it.

### Step 3 — slicing

**Slice Into Rooms.**

Settings live in `U_MyMaze → Slicing → Slicer`:

| Parameter | Default | What it means |
|---|---|---|
| `Room Size XZ` | (32, 32) | room size in cells |
| `Origin XZ` | (0, 0) | offset of the slicing lattice |
| `bDiscard Empty Rooms` | on | throw away rooms without a single wall |

After slicing, room frames appear in the viewport. The room under the cursor is highlighted brighter, so you can see at a glance which level whatever you're drawing will end up in.

At the same time a portal graph is built: rooms are linked if there is a traversable path between them in the play band. The runtime uses that instead of straight-line distance.

> If you can't see the room frames, look at the viewport overlay. If it says `rooms 0` and `NO SLICING — press Slice Into Rooms`, press Slice. `Clear Maze` and `Restore Snapshot` reset the slicing: the old slicing doesn't apply to the new grid.

### Step 4 — baking geometry

**Build Room Meshes.** Builds the meshes for all rooms and saves them to disk.

A report goes into the log:

```
Mesh bake: rooms 208, meshes 624, cells 630000 (hidden ones culled 580000, 92%),
quads 738608, triangles 1477216, collision boxes 12000; write failures 0;
packages unloaded 624; in 95.4 s
```

The cull percentage is the whole point of `bCullEnclosedCells`. If it's low, the maze is sparse and there will be a lot of geometry.

**Only what changed is rebuilt.** Each room carries a hash of what it is made of — its cells, the cells one step outside its bounds, the depth profile, the bake flags and the palette. A room whose hash has not moved is left alone:

```
Mesh bake: rooms 208 (of them unchanged 205), meshes 9, ... in 1.6 s
```

Cutting one hole no longer costs a full rebuild of the maze. The skirt of one cell around each room is why a wall built right against a seam rebuilds both rooms it touches, not just the one you were drawing in.

Two things a hash cannot see: meshes deleted or edited outside the plugin. If the meshes on disk and the plugin's idea of them have drifted apart, tick **Display → Build → Force Full Rebake** for one pass.

You can skip this step: export will bake the meshes itself, room by room, with the same rules.

### Step 5 — levels

**Export Rooms To Levels.** Creates one level per room and assembles the manifest.

Into the log:

```
Export: meshes for revision 14 are already baked — reusing them.
Export: rooms 208, levels 208, meshes 624 (of them reused 624), ...
```

`of them reused` is your check that the work isn't being done twice. Right after a bake it should equal the total. Anything less means those rooms changed between steps 4 and 5.

If you cancel an export, the levels and meshes already written stay on disk and the next run reuses them — but the **manifest is deliberately not rewritten**. A manifest listing part of a maze would give you a world with the rest of the rooms missing and nothing to say why; the previous, complete one is kept instead. Run the export again to finish.

The persistent level is **not touched** by this. Hooking things up is a separate button:

**Attach Rooms To Level** adds the room levels to the open map, **Detach Rooms From Level** removes them. This is deliberate: export creates assets and does not rewrite your map on its own.

After exporting, **save everything** — Ctrl+Shift+S.

---

## 6. You changed the maze — what to redo

| What you changed | What to do |
|---|---|
| Anything about the maze itself | `Apply Changes` (or `Apply Changes To Current Maze`) |
| Materials in the palette only | `Apply Changes` — the bake picks them up |
| Paths in the settings only | `Apply Changes` |
| Nothing yet, you just want to see the seams | turn on `Show Room Preview` — it re-slices nothing |

There is one list because there is one button. If you need to know which step is misbehaving, run them one at a time from **Advanced**:

| What you changed | Steps in Advanced |
|---|---|
| Painted with the brush | Build Depth Volume → Slice → Build Meshes → Export |
| Depth profile | Flatten To Plane → Build Depth Volume → Slice → Build Meshes → Export |
| Room size | Slice → Build Meshes → Export |

The plugin tracks this itself: the grid carries a revision number, and if it has diverged from the one the meshes were baked against, export silently rebuilds them. The `Status` line will show `meshes: stale`.

---

## 7. Running it in game

### Setting up the character

1. Open the player pawn blueprint.
2. Add a **Maze Streaming Component**.
3. Fill it in:
   - `Manifest` — the `DA_MazeWorldManifest` produced by export;
   - `Rules` — your `DA_MazeStreamRules`;
   - `bDrive Camera Bounds` — leave it enabled if the camera is clamped on X.

> **There are two fields called `Manifest`, and the game reads this one, not the other.** The maze asset has a `Manifest` too — but it is called **Built Manifest (output)** and shows what the last export wrote. It sets nothing, and the field on the character does not follow it.
>
> This matters in exactly one case, and it is a common one: **you set or changed `Maze Name`.** A named maze writes its own manifest, next to its own levels — `DA_MazeWorldManifest_Prod` in `Maps/Prod/` — while the character goes on loading the one it was pointed at some time ago. From outside it looks like "the build succeeded, the levels are on the map, Play is empty". Copy the name from `Built Manifest (output)` into the character's field and compile the blueprint.
4. Lock the player into the plane: in Character Movement enable **Constrain to Plane**, normal **(0, 1, 0)**.

The pool creates the streaming entries itself at runtime from the manifest. You do not need to add levels to the persistent map to play — `Attach Rooms To Level` is only for looking at them in the editor.

### Checking it works

Start PIE. Streaming debug appears in the corner of the screen: the player's room, the camera position, the frame size, and per room — visible / loaded-not-visible / loading / not attached.

Run around. Rooms should appear before they enter the frame and disappear behind you.

### If something is off

| Symptom | What to look at |
|---|---|
| No maze, just the floor | the manifest isn't hooked up to the component, or the export wasn't saved. The log names the manifest the component reads — compare it with `Built Manifest (output)` on the maze |
| No maze after setting `Maze Name` | the `Manifest` field on the character is left over from the previous build. See the note above |
| The objects didn't appear | look at the `objects` line in the export report: `skipped` means they did not fit, `unknown type` means the type is gone from the library |
| The objects are there but the player walks through them | they were placed in the Background or Foreground band. Collision exists only in **Play** |
| Rooms don't load ahead of you | `Camera Frame → Margin UU` is too small |
| Lighting flickers on unload | that's Lumen. Raise `Margin UU` and `Max Loaded Rooms` |
| Changing Margin had no effect | `Max Loaded Rooms` is lower than the ring needs. The pool trims by priority and throws out exactly the outer rooms |
| Character stutters while loading | the GC settings aren't in `DefaultEngine.ini` (§2) |

### How to compute the load distance

`Camera Frame → Margin UU` = how much world around the frame must be loaded.

```
Margin UU = (rooms of headroom) × Room Size XZ.X × Cell Size.X
```

A 32-cell room at `Cell Size` 50 is 1600 units. Two rooms of headroom — `Margin UU = 3200`.

Raise `Max Loaded Rooms` at the same time, or the ring won't fit in the budget. Watch the counter in the debug overlay: if it's pinned at the ceiling, raise it.

---

## 8. If something went wrong

| Symptom | Cause and fix |
|---|---|
| The mode panel is empty | no `Target Asset` selected — the viewport will say `MazeForge: pick a Target Asset in the mode panel` |
| The brush doesn't paint | the view looks along the plane — switch to Left / Right |
| Objects won't place | `Tool` is on **Cells**, or no type is selected — the panel will say `No object selected` |
| An object places but vanishes on build | it never fitted. The frame under the cursor says what is missing; the export counts these as `skipped` |
| An object floats or is sunk in | the asset's pivot is not where the anchor expects it. Adjust `Offset` on the type in the library |
| The log calls the type something other than the panel does | the panel shows `Display Name`, the log shows `Type Id`. The palette row shows both: `Placing: Box (Box1)` |
| No room frames | `Slice Into Rooms` hasn't been pressed. Check the counter in the viewport |
| Changes to build settings have no effect | `Build Settings` isn't set on the maze |
| The editor is sluggish while painting | you're editing in the volume. Press `Flatten To Plane` |
| `Build Depth Volume` complains in the log | too many cells. Reduce the depth or the area |
| Export crashes | `Rooms Per Flush` is 0 or too large. Set it to 8–16 |
| Meshes from an image don't match the original | check `Color Rules` and the tolerances, and `bFlipVertical` |
| Reconstruction from a mesh produced garbage | check the triangle count in the log. Low means Nanite is on and a fallback LOD is being read |

The first place to look, always, is the **Output Log, category `LogMazeForge`**. Every pipeline step writes a report with numbers there, and the answer is almost always already in it.

---

## 9. Quick reference

**First run on a project**

```
Plugin into Plugins/ → build → 4 lines in DefaultEngine.ini
Create: U_MyMaze, DA_MazeBuildSettings, DA_MazeStreamRules
Set Build Settings on U_MyMaze
Modes → MazeForge → Target Asset = U_MyMaze
```

**Working loop**

```
first build      draw with the brush → BackWall where needed → Save
                 Apply Changes
                 Ctrl+Shift+S

every edit after Change Current Maze
                 draw
                 Apply Changes To Current Maze
                 Ctrl+Shift+S
```

**Keys**

```
LMB                  place
Shift + LMB          erase
Ctrl + drag          rectangle
Ctrl + Shift + drag  erase rectangle
PgUp / PgDn          depth slice
hold Q               peek past the isolation
[ / ]                brush size
```
