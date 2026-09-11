# MazeForge — technical specification and implementation log

**Project:** *Saboteur 2: Avenging Angel* remake · Unreal Engine 5.8.2
**Plugin:** MazeForge 0.3.0 · three modules · MVP
**Document status:** as-built. It describes what was built, not what was planned.
**Russian version:** `MazeForge_TZ_AsBuilt_RU.md`.

The original specification sits alongside this document — `MazeForge_DESIGN_SPEC.md`, which remains in Russian. It was written before implementation, and some of its sections no longer match the code. This document is the source of truth; the divergences are listed in §11.

---

## 1. The brief

A level-prototyping tool for a 2.5D side-scroller. A designer must be able to:

1. obtain a maze — draw it by hand, generate it randomly, import it from an image, or reconstruct it from a reference mesh;
2. finish it off by hand;
3. slice it into rooms, build the geometry and lay it out across separate streaming levels;
4. get a runtime that loads and unloads those levels as play proceeds.

### Constraints fixed from the outset

- Modular architecture, small files, explicit interfaces, data in assets rather than in code.
- MVP focus. There are no months of development time available.
- Write no more code than is needed.

These three points dictated almost every decision below. Where the choice lay between "correct in general" and "works and is understandable", the second one won — but in a way that keeps the first one reachable.

---

## 2. Coordinate system

| Axis | Meaning |
|---|---|
| **X** | along the level, left to right |
| **Z** | height |
| **Y** | depth, the camera axis |

The camera sits at **+Y** and looks along **−Y**. The player is locked into the plane by `SetPlaneConstraintNormal(0, 1, 0)`.

This is load-bearing, not cosmetic. It follows from it that: a room is a rectangle in XZ spanning the full depth in Y; collision is built only in the play layer; the far face of the world at −Y is never visible in game and need not be built; overlay frames are drawn flat rather than as volumes.

### Depth bands

`FMazeDepthProfile` splits the depth into three bands:

| Band | Purpose | Collision |
|---|---|---|
| **Background** | décor behind the player | none |
| **Play** | the plane of movement | **yes** |
| **Foreground** | décor in front of the player, stage wings | none |

World zero on Y is the centre of the Play band. The default profile is 3 / 2 / 2; the production Saboteur 2 maze uses 9 / 4 / 9.

---

## 3. Architecture

### 3.1 Modules

| Module | Type | Contents |
|---|---|---|
| **MazeForgeCore** | Runtime, PreDefault | data, generators, slicers, assets, the room anchor |
| **MazeForgeStreaming** | Runtime, Default | the load pool, rules, the observer component, the subsystem |
| **MazeForgeEditor** | Editor, Default | draw mode, preview, mesh baking, level export |

Dependency rule: **Editor → Core**, **Editor → Streaming**, **Streaming → Core**. There is not a single reverse dependency.

What this buys in practice: the voxel grid, the slicers and the entire editor pipeline never reach the game build. The runtime sees only the manifest — a list of rooms with their bounds, neighbours and a triangle estimate.

### 3.2 Data flow

```
generator ──► FMazeGrid ──► slicer ──► FMazeRoomDesc[]
                  │                         │
                  │                         ├──► mesh bake ──► SM_*.uasset
                  │                         │
                  └─────────────────────────┴──► export ──► L_*.umap + manifest
                                                                   │
                                                       runtime: streaming pool
```

### 3.3 Data model

`FMazeGrid` is a sparse voxel grid:

```cpp
FVector    CellSize   = (100, 100, 100);
FIntPoint  SizeXZ     = (256, 128);
FMazeDepthProfile Depth;
FMazeWorldBorders Borders;   // whether to seal the world edges
FVector    WorldOrigin;
TMap<FIntVector, FMazeCell> Cells;   // occupied cells only
```

Sparseness here is not optimisation for its own sake: a dense 707 × 22 × 376 array is 5.8 million cells, of which 630,000 are occupied.

Cell types: `Empty`, `Solid`, `Floor`, `BackWall`. The grid describes mass and only mass: `Solid` and `Floor` are the maze itself, `BackWall` is the surface closing it off from behind (geometry, but not solid — the player never reaches it), `Empty` is everything else.

`BackWall` does not live in the same slice as the maze. The drawing plane of the maze is `Depth.PlaneCellY()`; the back wall goes to `Depth.BackWallPlaneCellY()`, the far edge of the depth. That is a property of the brush, not of the cell: selecting `BackWall` switches which slice the stroke lands in, so a stroke on one layer can never damage the other, in either direction.

Alongside the type each cell carries a `PaletteIndex` — the **surface variant**. Same geometry, different material: brick here, rock there, the wall of a building further on. The variants of a type are declared in `UMazeBuildSettings::Palette`, and a room whose cells use several of them bakes into one mesh with one material slot per surface.

`Ladder`, `Spawn` and `Marker` used to exist and are gone. A cell type can say "there is a ladder here" and nothing more — not which blueprint, not which way it faces, not where along the column it starts — so objects moved out into a separate spawner system with its own asset and its own brushes. The enum values are kept, hidden, so grids saved earlier still load; such cells are dropped on load.

### 3.4 Extension points

Three abstract classes, marked `EditInlineNew, DefaultToInstanced`:

- `UMazeGeneratorBase` — fills the grid;
- `UMazeRoomSlicerBase` — slices it into rooms;
- `IMazeMeshBuilder` — builds a room's geometry.

A new generator is a single subclass. It shows up in the panel's dropdown on its own, and its `UPROPERTY`s show up in the Details panel, without a line of UI code. That is the whole UI framework of the plugin: there are almost no custom widgets, everything is built by reflection.

---

## 4. The pipeline

Five steps, in exactly this order. In the mode panel they are laid out in numbered groups.

### Step 1 — obtain a maze

Four generators:

| Generator | Source | Key parameters |
|---|---|---|
| **Manual** | does nothing, the grid is drawn with the brush | — |
| **Random** | BSP subdivision | `MinSectorCells`, `FloorsMin/Max`, `ShaftCount`, `AgentHeightCells` |
| **Image** | PNG/JPG or `UTexture2D` | `ColorRules`, `TargetSizeXZ` |
| **Mesh** | `UStaticMesh` or OBJ | `VoxelSize`, `bFillUnreachable` |

Every generator writes **flat** — one cell per XZ column, into the `Depth.PlaneCellY()` slice. Volume is grown by a separate button. Why — §5.1.

### Step 2 — finish it by hand

The brush works in the plane, then `Build Depth Volume`. A snapshot of the grid is taken automatically before anything that would destroy work.

Two layers are drawn here, not one: the maze mass (`Solid`, `Floor`) in the play plane, and the back wall (`BackWall`) in its own slice at the far edge. `Fill Back Wall` and `Clear Back Wall` fill and clear the second one across the whole maze bounds; a window is a cell where the wall was erased. While the back wall is being painted the maze layer is pushed back and dimmed — §5.17.

### Step 3 — slicing

`UMazeSlicer_UniformGrid`: a regular lattice of `RoomSizeXZ` (32 × 32 cells by default), offset by `OriginXZ`. The base class computes fill, bounds and the **portal graph**.

The portal graph is not decoration. Two rooms are connected if a passable pair of cells exists across their shared face **in the play layer**. That gives the runtime graph proximity instead of Euclidean proximity: in a maze with thick walls, a room that is adjacent as the crow flies may be a hundred metres of travel away.

### Step 4 — mesh bake

`FMazeBakery` builds the meshes for every room, saves them to disk and records the grid revision number in the asset.

Splitting by band (`bSplitByDepthBand`) yields three meshes per room: `SM_<Room>_BG`, `_Play`, `_FG`. Only Play has collision. That makes it possible to dim or fade the foreground later without touching the play geometry.

Culling invisible faces (`bCullEnclosedCells`) is the main saving: a cell surrounded by solid on all six sides produces no quads at all.

### Step 5 — levels

`FMazeLevelExporter` creates one level per room, puts the mesh actors and an `AMazeRoomAnchor` into it, and assembles the manifest.

The persistent level is deliberately **left alone**. Streaming entries are created by the pool at runtime from the manifest, so MainLevel does not change and does not conflict in version control. Hooking the levels into the map is a separate, deliberate button: `Attach Rooms To Level`.

---

## 5. Key decisions and why they are what they are

This is the core of the document. Each entry is not a preference but a conclusion drawn from a specific failure.

### 5.1 The flat authoring model

**Problem.** After generating a volumetric maze, brush editing slowed down to the point of being unusable.

**Cause.** A brush stroke across a volume touches 22 cells per column, and the preview was rebuilt on every mouse movement.

**Solution.** Generators and the brush work in a **single plane**. Volume is an explicit `Build Depth Volume` step, reversible with `Flatten To Plane`. Plus a 100 ms throttle on preview rebuilds.

On a 707 × 376 map that is tens of thousands of cells instead of two million for the duration of editing.

### 5.2 Passability that accounts for the player's height

**Problem.** The generator produced mazes with unreachable rooms, even though a cell-wise flood fill reported them as connected.

**Cause.** A passage one cell high is connected at the cell level, but a human will not fit through it.

**Solution.** The entire passability model was rewritten around `CanStand(X, Z, AgentHeight)`. Flood fill, island finding and corridor carving all go through it. A passage shorter than the player is a wall, whatever cell-wise connectivity says.

Along the way it turned out that `FillRectIfSolid` in the corridor carver was silently cutting a corridor short on someone else's floor slab.

### 5.3 Nanite and reading the source geometry

**Problem.** Reconstruction from a mesh produced garbage. In the log: 4858 triangles instead of 26,458.

**Cause.** On a Nanite mesh, `LODResources[0]` is a **decimated fallback**, not the authored geometry.

**Solution.** Read `UStaticMesh::GetMeshDescription()` under `#if WITH_EDITOR`, falling back to `LODResources` with a warning in the log.

### 5.4 The voxelisation criterion

Three approaches were tried:

| Approach | Result |
|---|---|
| Triangle intersects the cell | walls double up: faces lie exactly on cell boundaries |
| Ray parity | 61,935 cells instead of 28,622 — needs a watertight mesh |
| **Cell centre inside the triangle's projection** | **correct** |

Bounds are computed from the **projection**, not from `GetBoundingBox()`: 16 stray triangles in the source OBJ stretched the depth axis.

Before any C++ was written, the reference OBJ was taken apart offline in Python by two independent methods. Both gave 28,622 solid cells, the result was rendered to a PNG and signed off. Only then was the code written.

### 5.5 PIE and package prefixes

**Problem.** In PIE, rooms were reported as loaded but never appeared on screen.

**Cause.** Found in `LevelStreaming.cpp:1657`: duplicating a level for PIE is guarded by the condition `LevelPackage == nullptr`. The pool was creating entries that pointed at non-PIE packages, and `FindWorldInPackage` returned the level from the **editor** world.

**Solution.** `StripPiePrefix` via `UWorld::RemovePIEPrefix`, plus a refusal to create new streaming entries inside PIE.

### 5.6 Eviction by priority

**Problem.** Rooms with a desire of 1.00 would not load.

**Cause.** The occupied-slot counter was computed once and never decremented, and there was nothing to unload: every desire exceeded `UnloadThreshold`.

**Solution.** The budget is recomputed every tick: rooms are sorted by desire, the first `MaxLoadedRooms` stay, the rest are evicted. Unloading is deferred while a load is in flight.

### 5.7 Export crash on address-space exhaustion

**Problem.** The editor crashed with an exception on the render thread **after** everything had been saved to disk successfully.

**Cause.** Found in the project log, not guessed at:

```
LogRHICore: Warning: Total reserved resource allocated virtual size (258 GB)
exceeds the budget (256 GB)
```

A depth of 22 → 2,089,626 cells → 624 meshes → 1,477,216 triangles, and all of it held in memory at once. What ran out was not RAM but the reserved RHI address space.

**Solution.** Flush in batches: every `RoomsPerFlush` rooms, the finished packages are unloaded, `FlushRenderingCommands` is called and garbage is collected. The levels are already on disk by then and the manifest references them through soft pointers — there is nothing to lose.

### 5.8 A hitch when rooms load and unload

**Problem.** The character stuttered at precisely the moment a room was streamed in or out, with the pool full.

**Cause.** `World.cpp:5185`: on any growth of the level unload queue the engine calls `ForceGarbageCollection(true)`, and that (`UnrealEngine.cpp:2136`) sets `bFullPurgeTriggered` — a **full blocking garbage collection on the next tick**. Not an incremental one.

**Solution.** Four lines in `DefaultEngine.ini`:

```ini
[SystemSettings]
s.ForceGCAfterLevelStreamedOut=0
s.ContinuouslyIncrementalGCWhileLevelsPendingPurge=0
gc.TimeBetweenPurgingPendingKillObjects=120
s.AdaptiveAddToWorld.Enabled=1
```

Confirmed by toggling the CVar in the console on a live scene: `s.ForceGCAfterLevelStreamedOut 1` brings the hitch straight back.

**The price:** garbage from unloaded rooms accumulates for longer. `Mem` in `stat unit` needs watching.

### 5.9 Reusing baked meshes

**Problem.** Steps 4 and 5 built the same geometry twice.

**Solution.** Two numbers in the asset: `GridRevision` increments on any change to the grid (through a single point, `NotifyGridChanged`), and `BakedRevision` is written by the bake. If they match, export loads the meshes from disk and only assembles the levels.

The mark is set **only after a complete bake pass**. On an interrupted bake, some rooms would be left with meshes from the previous revision, and export would take them for fresh ones and silently assemble levels out of stale geometry — a bug that would surface in the game, not in the editor.

Export nevertheless stays self-sufficient: if there is no baked mesh, it builds one itself.

### 5.10 A snapshot instead of trusting Undo

Undo is fine while the editor is open and useless after a restart. The grid snapshot lives inside the asset itself. It is taken automatically before `Clear Grid` and before generation with clearing enabled.

`Restore Snapshot` **swaps** the grid and the snapshot rather than overwriting: a mistaken restore is undone by pressing it a second time. A one-way button would itself destroy work — exactly what the snapshot exists to prevent.

### 5.11 Safe re-export

Every generated actor is marked with `GeneratedTag`. A repeat export deletes **only the marked ones** and leaves the designer's décor alone. Without this, the first edit to the maze would have wiped the team's work.

### 5.12 The panel is a customization, and it has to be

The panel reads top to bottom the way the job goes: Target, Generate, Brush, Display, Snapshot, Build, Edit, Advanced. Three things stand between that and what the property system produces on its own.

**The order.** `PropertyCustomizationHelpers.cpp`, function `GetCallInEditorFunctionsForClassInternal`, sorts buttons by the `Category` string **alphabetically** and creates the categories in that same order — which here would be Advanced, Brush, Build, Display, Snapshot, Target. The first version of the panel worked around this by naming the categories `0 Edit`, `1 Generate`, `2 Draw` and so on, and the digits then showed up in the UI. `IDetailCategoryBuilder::SetSortOrder` says the same thing without spelling it into the labels, so the names are plain again.

**Disabled buttons.** A `CallInEditor` button is always enabled; there is no hook for a condition. Three of them must have one — `Generate Maze` against a grid whose generator is the manual one, and the two edit-cycle buttons before a slicing exists — so those three are built by hand as custom rows with `IsEnabled_Lambda`. They are bound rather than computed once: the generator is chosen in a different panel, and the button has to notice without anyone refreshing this one.

**A disabled button explains itself.** Under each one is a line saying why: `Current mode: manual drawing — there is nothing to generate.`, `No maze yet — build one with Apply Changes first.` A greyed-out button with no explanation reads as a broken one.

The rest stay `CallInEditor`. Hand-building a button that needs no condition would be code written to produce what the engine already produces.

### 5.12a Two Apply buttons that do the same thing

`Apply Changes` calls `ApplyChangesToCurrentMaze` and adds nothing. They are still two buttons.

They differ in what they mean, not in what they do. One is the first build of something that has only ever been drawn; the other closes an edit cycle that `Change Current Maze` opened. Sharing the implementation is right — there is one build, and a second copy of it would drift. Sharing the button would mean the label is wrong half the time, and in a panel whose whole point is that it reads like the job, that costs more than a duplicated row.

`Clear All Changes` is the counterweight, and it undoes more than the drawing: the room levels come out of the map, the grid and slicing and bake state are erased, and the manifest reference is cleared. The order matters — the levels first. Clearing the grid while they are still attached would leave their geometry standing in the world with no room, no slicing and no asset behind it: visible, selectable, and no longer removable from this panel.

The level and mesh assets are deliberately left on disk. It is the one operation a snapshot does not cover, and the next build overwrites them anyway.

### 5.13 `config` only works on soft references

The mode panel settings are marked `config` so that they survive an editor restart. Asset references had to be made `TSoftObjectPtr`: `UhtObjectPropertyBase.cs:655` strips ordinary object properties of the ability to be config, and `UhtSoftObjectProperty.cs:46` gives it back explicitly.

### 5.14 The overlay is drawn in SDPG_Foreground

The grid planes and the band boundaries pass through the preview cubes. In the normal depth group a line passes the depth test one frame and fails it the next — the overlay flickers at the slightest camera movement.

### 5.15 The back wall is a layer of its own, not a cell type in the same plane

The question that had to be settled before a line of code: is the back wall **a cell type in the same plane, or a second drawing plane?**

The whole authoring model is flat — one cell per XZ column, in the `Depth.PlaneCellY()` slice. The back wall belongs in a different slice by its very meaning. Making it a type in the same plane and letting `Build Depth Volume` sort them out afterwards is cheaper; a second layer is honest.

The second layer won, and the reason is the eraser. In one plane, a column can hold either the maze or the wall behind it, never both — so drawing a wall behind an existing wall of the maze would have to destroy the maze, and there is no version of that which is not a bug. With a separate slice both exist in the same column, `Shift + LMB` on the maze does not touch the wall, and the two brushes never interfere.

The cost is one number: `FMazeDepthProfile::BackWallPlaneCellY()`. The rest is routing in `GetDepthRange` — three lines.

### 5.16 One material slot per surface, not one mesh per material

A room where part of the mass is brick and part is rock could be baked as two meshes. It is baked as one, with two polygon groups and two material slots.

Two meshes per surface would multiply everything a room costs — draw calls, packages on disk, actors in the level, entries in the manifest — by the number of materials the designer happens to use, and the designer is the one person who must not have to think about that. Polygon groups cost nothing: `FMeshDescription` supports them natively, and the material array is filled in the same order the groups are created.

The surface list is built per room, not globally, so a room that uses one variant gets exactly one slot.

There is a bug this uncovered and fixed on the way. `bSplitByDepthBand = false` — the single-mesh path — was baking **only the play band**, silently dropping everything in the background and foreground. That went unnoticed while the background held only decor. With the back wall living in the background it would have dropped the wall itself on every single-mesh export.

### 5.17 Isolating the active layer moves the component, not the instances

Painting a back wall inside a deep niche is blind work: the maze mass stands between the camera and the wall. So the layer being painted stays where it is and the rest are pushed away from the camera and dimmed to 30%.

Two earlier attempts got this wrong, and both failures were instructive.

**The first** pushed the *active* layer towards the camera. Selecting `Solid` made the maze disappear. At depth 22 and 50-unit cells that is 1100 units forward — straight through the near clipping plane of the orthographic viewport. The back wall survived it only because it starts at the far edge and had room to travel. Pushing the others back instead cannot clip anything, and it has a second virtue: the layer being drawn never moves at all, so what the brush lands on is exactly what is under the cursor.

**The second** pushed the others back, correctly, but did it by offsetting every instance transform. Now the back wall vanished — and reappeared after switching the viewport to perspective and back. That symptom is the entire diagnosis. It is not clipping; clipping does not repair itself when you change view and come back. A `UHierarchicalInstancedStaticMeshComponent` culls against a cluster tree built from the instance data, and instances that jump a long way leave that tree describing where they used to be. The viewport switch forced a rebuild, which is why the cells came back.

The fix is to move the **component** and leave the instances alone. That is an ordinary transform change: bounds and culling follow it the way they follow any moved component, and the instance data never changes. Two ordering details matter and are commented in the code — the offset is applied *after* `AddInstances`, because instances are given in world space and that conversion reads the component transform, and the component is reset to zero before refilling, or a stale offset gets baked into the instances themselves.

It is a shift, not a snap to a single plane. Snapping would pile every cell of a built volume onto the same Y, and identical cubes sharing a position fight over depth and shimmer.

**A peek key, not an opacity slider.** Isolation solves one problem and creates a smaller one: the layer brought forward now hides what it covers, so painting over a large stretch of back wall loses sight of the floors that were there. The obvious answer is a transparency slider, and it is the wrong one twice over.

Mechanically it is not cheap: the preview is drawn with `/Engine/BasicShapes/BasicShapeMaterial`, which is opaque, and blend mode is a *static* material property — a `UMaterialInstanceDynamic` cannot change it, so this would mean shipping a translucent material with the plugin, and the plugin has no content folder at all. And the result would be poor: after `Build Depth Volume` a column holds dozens of identical cubes, and heavily overlapping translucent surfaces sort badly against each other.

Dimming the far layers further — the obvious cheap version, since `DimFactor` is already a constant — does not address the problem either. The layers being dimmed are the ones already *behind*; what hides the floors is the layer in front, and making the ones behind fainter cannot reveal them.

So it is a held key (`UMazeEdModeSettings::PeekKey`, `Q` by default, configurable): while held, isolation is suspended and everything returns to its true depth order. It costs nothing, has no sorting artefacts, and needs no value to be tuned. Two details: a key release is not guaranteed to arrive — hold, alt-tab, release outside the editor — so `ReconcilePeek` re-checks the physical key state on mouse movement, and the overlay shows `PEEK` while it is held, because what is on screen then is not the layer order a stroke would land in.

### 5.18 The grid holds mass, objects belong to a spawner

`Ladder`, `Spawn` and `Marker` were cell types. They are not any more, and the grid is now `Empty`, `Solid`, `Floor`, `BackWall` — mass and nothing else.

A cell type is one byte. It can say "there is a ladder here" and stop there: not which blueprint, not which way it faces along X, not where along the column it starts, not what offset it has inside the cell. Every one of those questions has an answer for every object, so the byte was never going to be enough — and the moment it is not enough, a cell type is the wrong container.

The evidence was already in the code. A grep for the three types found **writers and not one reader**. The random generator marked ladders, the palette coloured them, and nothing anywhere consumed them. They had been data waiting for a system that had not been designed, and the design, once it arrived, wanted a different shape: a separate spawner asset with a list of placements — cell coordinate, type, variant, rotation, anchor, offset, stable id.

The values stay in the enum, hidden with `UMETA(Hidden)`, because they serialise as raw bytes: deleting them would leave cells in already-saved grids holding a number that no longer names anything. `UMazeGridAsset::PostLoad` removes such cells once, silently, without bumping the revision — the mass of the maze does not change, so the baked meshes stay valid.

The practical result is the reason this was worth doing now rather than later: the maze can be drawn today and the spawner built alongside it, as two independent tracks. Nothing drawn now will need redrawing when placements arrive.

The generator keeps carving what a ladder needs — openings in slabs (`LadderChance`, `LadderOpeningCells`) and shafts. That is geometry. It simply no longer labels it.

### 5.19 Rebuilding one room, not two hundred and eight

Reuse used to be all or nothing: `GridRevision` against `BakedRevision`. Cut one hole in one room and the honest answer to "has anything changed" is yes — so all 208 rooms were rebuilt to rebuild one.

Each room now carries a hash of what it is made of (`UMazeGridAsset::ComputeRoomHash`), stored per room in the asset. The bake and the export both skip a room whose hash has not moved.

What goes into the hash is the whole design, and two entries in it are not obvious:

- **A skirt of one cell.** Face culling asks whether the neighbouring cell is solid, and the neighbour of a cell on the room's edge lives in the room next door. Hashing only the room's own bounds would leave a wall built right against a seam rebuilding one of the two rooms it changes, and the other keeping a face that should no longer exist. One cell is exactly right and was checked against the code, not guessed: `IsFullyEnclosed` and the culling test both read ±1 and nothing reads further.
- **The names go in as strings.** `GetTypeHash(FName)` is the name's slot in the global name pool, assigned in order of first creation during that run of the editor — the engine documents it as unstable and it is. Using it would have made every hash miss after a restart and rebaked the whole maze once per session: the exact work the hashes exist to remove, moved rather than removed. `GetTypeHash(FString)` is a table-driven CRC and does not move.

Also hashed: the grid's cell size and origin, the depth profile, the world borders (solid mass the grid never stores as cells), the bake flags, and the palette — walked in sorted key order, because a `TMap` promises no iteration order and a hash that depended on one would flip between runs.

Deliberately NOT part of the test: whether the meshes are on disk. That was tried. A room whose every band is empty produces no file at all, so "no file" and "not baked" are different questions, and answering one with the other left the bake and the status line permanently disagreeing about such rooms. What a hash cannot see — files deleted or edited behind the plugin's back — is what `Force Full Rebake` is for, and the export rebuilds any band it fails to load anyway.

### 5.20 What the review of this change found

The incremental bake was reviewed before it was committed, and four of the findings were real defects in work that looked finished:

1. **The `FName` hash above.** The feature would have worked perfectly until the editor was restarted.
2. **`Build()` could not say why it returned false.** Four different code paths returned `false`: a null request, zero quads, `CreatePackage` failing, `CreateMeshDescription` failing. Only the second is "nothing to build here". The callers treated all four as that, so a bad `Mesh Package Root` would have recorded every room as successfully baked while writing nothing. `FMazeBakeResult::bFailed` now separates the two.
3. **`Modify()` came after the write.** The transaction records the state as it is when `Modify()` is called, so calling it afterwards records the new state as the old one — an undo of the preceding paint stroke would have discarded the bake record while leaving the files.
4. **A cancelled export saved a truncated manifest.** `Manifest->Rooms` was cleared up front and filled as the loop went; cancelling at room 40 of 208 saved a manifest describing 40 rooms, and the runtime sees only the manifest. The rooms are now staged in a local array and the manifest is left untouched unless the pass completes. The bakery had exactly this care already; the exporter did not.

None of these would have shown up in the first test — that is what makes them worth recording.

### 5.21 Preview Rooms runs the real slicer

The room grid shown by `Show Room Preview` is produced by `Slicer->Execute` into a scratch array that is thrown away after it is drawn. Nothing is written to the asset.

That it is the same call `Slice Into Rooms` makes is the whole point. A preview drawn by a second implementation agrees with the real one until somebody changes one of them, and then it lies — quietly, in the direction of whichever was easier to write. Here there is nothing to diverge from.

It is recomputed when the grid or the settings change, not per frame: `Execute` counts every room's contents and builds the portal graph, which is far too much work to repeat while drawing. And only one set of frames is ever drawn — the prospective slicing or the stored one, never both, or every seam would appear twice with no way to tell them apart.

### 5.22 One symptom, five causes

Over two days the mode looked broken in a way that never resolved into a single description: the preview would empty itself, the brush would go dead, the cursor would vanish, and everything came back after switching modes or restarting the editor. It was five separate faults, and each one masked the next.

**1. The preview never asked for a frame.** Every `Invalidate` in the mode sat inside a mouse or key handler. Drawing worked because the mouse invalidates on every move; buttons did not, because nothing asked the viewport to redraw after them. A hierarchical instanced component makes it worse: it draws nothing until its cluster tree is built, and that build is asynchronous — off the mouse, the tree finishes after the last redraw and there is nobody left to request another. Fixed by invalidating on every rebuild, and by finishing the trees synchronously when the rebuild is not part of a stroke.

**2. A stroke could be left open for ever.** `bPainting` is returned by `StartTracking`, so while it is set the viewport believes the mode is dragging: it captures the mouse and hides the cursor. Nothing cleared it except a mouse release, so one swallowed release — or a panel button pressed mid-drag — left the mode with no cursor, no painting and an open transaction until the editor was restarted. `AbortStroke` now closes it on entering the mode, on leaving it, and before starting another.

This one had been latent for weeks. Fix 1 is what made it visible: the redraw was made conditional on `!bPainting`, which tied "the display does not update" to a flag that could stick. A new symptom on an old bug.

**3. Detached levels were still counted as attached.** The preview skips rooms whose levels are in the map, because their real geometry is already on screen. The check was `GetLoadedLevel() != nullptr`. `UEditorLevelUtils::RemoveLevelFromWorld` takes the level out of `World->Levels` and reports success, but the `ULevelStreaming` entry survives with `GetLoadedLevel()` still returning the orphaned level until the next garbage collection. So the rebuild that ran immediately after `Change Current Maze` skipped every cell in every room — and with two rooms covering the whole maze, that is every cell. The backdrop and the world borders are suppressed by the same flag, so the viewport went completely empty while the grid was perfectly intact. Fixed by asking `World->GetLevels().Contains(Loaded)` instead: the membership list is updated by the removal itself and cannot lag behind it.

**4. `T <= 0` is wrong in an orthographic viewport.** Added while chasing the others, as a diagnostic. An ortho viewport draws the whole scene along its axis regardless of where its camera sits on that axis — UE puts the ortho near plane at `-HALF_WORLD_MAX` precisely so that it does. When the ortho camera's Y drifted into the depth volume, the background slice still picked and the play slice did not, and the brush went dead in the one slice that matters. The rejected hits were plainly visible on screen, which is the proof: geometry behind the camera in any meaningful sense would not have been drawn.

**5. The preview actor was born inside a room level.** `SpawnActor` without an override puts the actor in the world's **current** level, and `Attach Rooms To Level` makes a room level current. Enter the mode after an attach and the preview belongs to a room; the next `Detach` destroys it along with that room. The pointer still tests non-null, so `Rebuild` was called on a dead actor and did nothing. Restarting the editor was the only cure, which is exactly what was observed. Fixed with `SpawnParams.OverrideLevel = World->PersistentLevel`, and by re-creating the actor on any rebuild that finds it invalid.

**What the two days actually cost, and what shortened them.** Four rounds went into diagnosing symptoms rather than causes, and the turning point was making the tool report its own state: the overlay now says why there is no cursor (`NO CURSOR: outside the grid: X -2 Z 33`, `the drawing plane is behind the camera`), and the log says how many rooms the preview suppressed and how many cells it was handed. The first of those messages caught fault 4 — which was mine, introduced by the diagnostics themselves.

The rule this earns a place for: **when a failure has no message, the first fix is the message.** Three different causes look identical from outside as "I cannot draw", and no amount of reasoning separates them; one printed number does.

### 5.23 A maze owns where its output lands, the build settings own how it is built

Room ids are not unique across mazes. Every uniform slicing starts at `R_000_000`, so two mazes sharing one set of build settings write the same level names, the same mesh names and the same manifest. Building the test maze overwrote the production one, silently, and the only sign was that the production maze had quietly become the test maze.

Worse than the overwrite: `DetachRooms` matched streaming levels by the level package root alone, so taking one maze off the map took the other one with it, and the preview counted another maze's loaded levels as its own rooms.

The fix is one field on the grid asset, `MazeName`, empty by default so existing mazes keep their assets byte for byte. Set it and everything that maze produces moves into a compartment of its own — a subfolder for the levels, meshes and manifest, the name carried in the asset names, the same compartment in the Outliner.

**Why not a second copy of the build settings.** It would have worked, and it would have duplicated the palette, the surface variants and the bake flags along with the paths. Those answer "how to build" and are worth sharing across every maze in the project; only "where it lands" belongs to the maze. The same split had already proved itself once — the build settings are shared, the grid is not — and this is the same line drawn one field further along.

**Why the compartment is a folder and not just a prefix.** `DetachRooms` and the preview's room suppression now filter on `<LevelRoot>/<MazeName>`. Matching a path has nothing to parse and no way to parse it wrong; matching a name does. The name prefix is still carried in the asset names, but only so the Content Browser, the Levels panel and the Outliner — all of which show names without their folders — do not present two identical rows.

### 5.24 Switching the target detaches, and deliberately does not attach

Changing `Target Asset` takes the previous maze's levels off the map and deletes the Outliner folders it created. It does not put the new maze's levels there.

The asymmetry is the decision. Detaching is fast, loads nothing, and removes the one genuinely confusing state: another maze's real geometry standing behind this maze's preview cubes. Attaching loads every room level, and a dropdown that thinks for ten seconds is a bad dropdown. It also changes the designer's map, which is a deliberate act and belongs behind a button — and `Apply Changes` already ends with an attach, so the new maze arrives on the map at the moment it is actually wanted.

The handler sits in `OnSettingsChanged` and not in `BindAsset`, though `BindAsset` is where the previous target is conveniently in scope. `BindAsset` is also how entering and leaving the mode set up and tear down, and leaving the mode must not unload the designer's levels.

Folders are deleted deepest first, and only the maze's own: the per-room folders and, when the maze is named, its compartment. The shared root is never touched — other things live there. The path arithmetic is shared with the export through `MazeExport::OutlinerRoot`, because the export creates exactly what the detach deletes; a second copy of it would leave folders behind the day one of them changed. That is the third name pulled into `MazeExportUtils` after the mesh name and the level name, each time because the duplicate was a bug waiting for its day.

### 5.25 The object belongs to the spawner, its number belongs to the level

A placement lives in `UMazeSpawnAsset` — cell, type, depth band, anchor, rotation, offset, identity. Why not in the grid is 5.18. This is about the identity, because it is the one thing that crosses the line between the editor and the game.

The number is handed out **at spawn time**, and only to what actually stands in a level. Not at the click: drawing creates nothing, and a point that has never been in the world has nothing to identify. Not in a batch before the export either: a placement the export refused to build gets no number — otherwise "has an id" would stop meaning "this thing stands in a level" the first day a wall was redrawn under a crate. `NextId` only ever goes up: a number already handed out may be named by a save on somebody's disk, and reusing it is a save pointing at the wrong thing.

The number reaches the game as a `UMazeObjectIdComponent` — `PlacementId`, `TypeId`, `RoomId`. An actor tag would have cost no new code, and was rejected: the number is meant to key a registry of what the player has done to each object, and a key that exists only as the tail of a string is one the compiler cannot check. A field is read through `GetPlacementId` and a typo is a build error; a tag is read by parsing and a typo is a silently empty save. `AddInstanceComponent` sets `CreationMethod = Instance` itself, so the component serialises into the level and survives the construction script re-running on a blueprint actor.

The spawn asset is written to disk together with the manifest, and **before** the export's cancellation check. Levels already written have the numbers baked into them; a number that exists in a level but not in the asset is worse than none at all — the next export would hand that same number to something else.

The asset and the library are held by an `FGCObjectScopeGuard` for the duration of the export. They are reached through soft pointers, and a soft pointer is not a reference as far as the collector is concerned, while the export unloads packages in batches and collects garbage as it goes. Without the guards, a large maze could see the asset swept up mid-run, taking every number handed out so far with it.

**Known limitation.** Duplicating the grid asset does not duplicate the spawn asset — both mazes end up pointing at one. Duplicating the spawn asset copies the placements together with their numbers, so two different objects in two mazes carry the same identity. With no state registry yet, nothing breaks; when there is one, the copy will need its numbers cleared and its `NextId` kept. Written down here so it is not discovered from a bug.

### 5.26 The hand is allowed to be wrong, the export is not

The brush places the object where you clicked, even when it does not fit. A crate half sunk into a wall is sometimes exactly what the scene wants, and a brush that argues is a bad brush. The one that has to obey the rule is the generator: it places blind, and a hundred "never mind"s become a hundred objects inside the mass.

The export checks again, against the geometry as it stands **now**: the wall the object was leaning on may well have been redrawn since. Three outcomes. The stored anchor still holds — placed, silently. The anchor is gone but another one fits — placed, counted as `re-anchored`, and the rotation recomputed, because the stored angle was resolved against the anchor it has just lost and on `AwayFromWall` that is visible to the eye. Nothing holds it — **not placed**, and reported.

Skipping was chosen deliberately over "place it anyway and complain": the level stays clean. The price is an object vanishing from the scene without a sound, and that price is exactly what the report has to cover. Hence the line `objects N (re-anchored R, skipped S, unknown type U)` and a separate warning per skip, with the type and the coordinates.

One hole found along the way is closed separately: a room with objects but no geometry gets no level at all, and its objects would have disappeared without being anybody's mistake. It is the one case where the counter grows for a reason other than geometry, and it has its own line.

**What was missing, and that was my own failure.** The brush's warning went to the log. Nobody reads logs while painting. Eight crates were placed in a row, each with its own warning, and all eight vanished twenty minutes later, at the export. Worse: the text said "does not fit" and stopped there, and "does not fit" covers five different faults — outside the grid, standing inside mass, nothing below, nothing above, nothing beside — with five different fixes.

`MazePlacement::DescribeMisfit` now names the reason in words, and the verdict sits **in the frame, under the cursor, before the click**: `Box1 fits here`, or `Box1 WON'T FIT: X 2 Z 26 is mass`. One function serves the brush, the frame and the export, so the three texts cannot drift apart. Only the anchors the type actually allows are named: telling somebody there is no ceiling above a crate that was never allowed to hang is noise.

This is the second bill presented by the rule from 5.22. Keeping it turned out cheaper than breaking it.

### 5.27 The object sits on the edge it is anchored by, not in the middle of the cell

`CellToWorld` returns the centre of a cell, and the first version put the object's origin there. The crates ended up floating half a cell above the floor.

Props are modelled with the pivot on the face that touches the world: a crate's at its base, a hanging lamp's at its top, a torch's at its back. So the origin now goes **on the anchor's edge**: Floor — the bottom of the footprint, Ceiling — the top, Wall — the side that actually has the mass, Free — the centre. Which side the wall is on is asked of the grid again rather than stored: the placement remembers that the object leans on a wall, never which wall, and re-deriving it keeps that fact in one place — the same place `ResolveRotation` goes to. `Type.Offset` remains the escape hatch for an asset whose pivot is somewhere else.

A consequence worth knowing: `Free` centres. A type that allows both `Floor` and `Free` sits on the edge when there is a floor, and in the middle of the cell — half a cell higher — when there is not. Which is why `Free` is not the way to stack objects, however much it looks like it.

### 5.28 Two fields called Manifest, and the game reads the one that is not on display

The grid asset has a `Manifest` — the export fills it in. The `UMazeStreamingComponent` has a `Manifest` — a person fills it in, once, by hand. The game reads the second. The first does not follow it and never did.

While there was one maze there was no difference. `MazeName` (5.23) created one: a named maze writes **its own** manifest, next to its own levels, while the character goes on loading the one it was pointed at some time ago. The symptom is an empty scene on Play with a perfectly good build and the levels sitting on the map.

Three things made this cost more than it should have.

The field on the grid asset is greyed out, updates itself, and sits directly under `Maze Name` — it reads as the answer. Its comment said "this is where the runtime takes it from", which is precisely what does not happen. Renamed to **Built Manifest (output)**, tooltip rewritten.

The pool's error said "room is not attached to the persistent level" and sent the reader to press `Attach Rooms To Level` — a button that was already correct, on a map whose rooms were already there. The real cause was another maze's manifest. It now checks whether the level package exists on disk at all: it does not, so the manifest is asking for a maze never built under that name, and the text points at the field on the character; it does but is not attached, and the old text stands. Once, on the first miss, it prints what the map **actually** holds.

And above all: the streaming tick used to leave like this.

```cpp
if (!Manifest || !RulesAsset || Manifest->Rooms.Num() == 0) { return; }
```

Three fatal conditions, one silent return, once per frame, for ever. Nothing streams and nothing is said — the worst shape a failure can take: there is no symptom to search the log for, so the hunt starts at the wrong end every time. It now says it once and specifically: which field is empty and with what path, or that the manifest lists no rooms and that this is nearly always a leftover from a build under a different `Maze Name`. The flag clears as soon as streaming works, so a break later still gets its line. The component's `BeginPlay` additionally names the manifest it reads — that one line was what stood between an evening and a minute.

---

## 6. Method of work

One habit paid off more than all the others put together:

> **Read the artefact, don't reason about it.**

Engine source instead of assumptions about the API. The project log instead of guesses about the cause of a crash. Offline analysis of the data instead of debugging in the editor.

Counter-examples from this same body of work, for completeness:

- `r.Lumen.HardwareRayTracing 0` was proposed without reading `DefaultEngine.ini`, where it was already set to `False`. Pure wasted motion.
- `UWorld::AddToWorld: ... took (less than) 47.69 ms` was read as a measurement. It is in fact the delta of a `static double LastElapsed` between arbitrary time-limit checks, and Epic's comment says outright that it can fire after an unrelated stall.
- `stat unit` — a smoothed average — was used for three rounds to hunt a single-frame spike, instead of `stat unitgraph`.
- Ray-parity voxelisation was proposed with confidence and gave 61,935 cells instead of 28,622.

---

## 7. The streaming runtime

### Composition

| Class | Role |
|---|---|
| `UMazeStreamingComponent` | the observer on the pawn: references to the manifest and the rules |
| `UMazeStreamingSubsystem` | 10 Hz tick, debug overlay |
| `UMazeRoomPool` | `ULevelStreamingDynamic` entries, budget, eviction |
| `UMazeStreamingRule` | a rule: writes a "desire" to keep a room loaded |

### Rules

| Rule | What it does |
|---|---|
| **Camera Frame** | everything in frame plus a margin must be loaded |
| **Player Radius** | a safety net based on proximity to the player |
| **Portal Graph** | neighbours through the portal graph, not as the crow flies |
| **Velocity Predict** | lead based on velocity |
| **Vertical Motion** | falling down a shaft: the rooms below are needed sooner |

Desires are summed, and the budget cuts by priority. Two thresholds (`LoadThreshold` 0.35 / `UnloadThreshold` 0.15) give hysteresis — with a single threshold a room sitting on the boundary would load and unload every frame.

---

## 8. Diagnostics

| Where | What it shows |
|---|---|
| Viewport overlay in the mode | depth slice, band, brush size, cells, rooms, cursor |
| The `Status` line in the panel | plane or volume, cells, rooms, mesh state, snapshot |
| Streaming debug in PIE | room, player, camera, frame; visible / loaded-but-not-visible / loading / not attached |
| `LogMazeForge` | reports from every step, with numbers |

An example export report:

```
Export: meshes for revision 14 are already baked — reusing them.
Export: rooms 208, levels 208, meshes 624 (of them reused 624), ...
```

The number in brackets is a direct check that no work is being duplicated.

---

## 9. File map

```
MazeForgeCore/
  Data/          MazeTypes, MazeGrid, MazeRoomDesc, MazeSpawnTypes
  Assets/        MazeGridAsset, MazeBuildSettings, MazeWorldManifest,
                 MazeObjectLibrary, MazeSpawnAsset
  Generators/    Base, Manual, Random, Image, Mesh, MazeLayout2D
  Slicers/       Base, UniformGrid
  Spawner/       MazePlacementRules, MazeObjectIdComponent
  Actors/        MazeRoomAnchor

MazeForgeStreaming/
  MazeStreamTypes, MazeRoomPool, MazeStreamingComponent,
  MazeStreamingSubsystem, MazeStreamingRulesAsset
  Rules/         CameraFrame, Radius, PortalGraph, VelocityPredict, VerticalMotion

MazeForgeEditor/
  Mode/          MazeEdMode, MazeEdModeSettings, MazeEdModeToolkit,
                 MazeEdModeSettingsDetails
  Render/        MazeGridRenderer, MazePreviewActor, MazeEditorStyleAsset
  Export/        MazeBakery, MazeLevelExporter, MazeLevelAttacher,
                 MazeMeshBuilder_Faces, MazeCollisionBuilder, MazeExportUtils
  Assets/        MazeGridAssetFactory, AssetDefinition_MazeGridAsset
```

---

## 10. What is done

| Stage | Contents | Status |
|---|---|---|
| P1–P2 | Data, assets, draw mode, preview | done |
| P3 | Streaming pool, rules, debug | done |
| P4 | Random generator with height-aware passability | done |
| P5 | Generation from an image | done |
| P6 | Reconstruction from a mesh, with cavity filling and volume | done |
| P7 | Flat editing, volume, snapshots | done |
| P8 | Batched export, memory release | done |
| P9 | Pipeline panel, style preset, folder configuration | done |
| P10 | Back-wall brush, surface variants and swatch palette, layer isolation, grid reduced to mass | done |
| P11 | Panel rebuilt around the job: one-press build, edit cycle, conditional buttons, room preview | done |
| P12 | Per-maze output compartment, target switching that cleans up after itself | done |
| P11 | Edit cycle for a built maze, per-room incremental bake, preview refresh on level changes | done |
| P13 | Spawner, phase 1: object library, spawn asset, placement rules, object brush, export into room levels with identity | done |

---

## 11. Divergences from the original specification

| Spec section | What changed |
|---|---|
| §4 | The generator no longer owns `SizeX` / `SizeZ` — the bounds belong to the grid |
| §4 | `AgentHeightCells` is absent from the spec; it appeared after the unreachable-rooms investigation |
| §5 | `PixelsPerCell` was replaced by `TargetSizeXZ` |
| §5 | The mesh generation mode is not in the spec at all |
| §6.2 | The "mesh bake" and "level export" steps were split, and revision-based reuse was added |
| §2.35 | The flat authoring model and explicit volume growth are not described in the spec |

---

## 12. What is left

1. **A stress test of the full pipeline** on 208 rooms with mesh reuse — now doubly worth doing, because the incremental path has never been run on the complete maze either.
2. **A residual micro-hitch** in the heaviest places. This is no longer garbage collection but `AddToWorld` — registering the components of a new room. Tunable through `s.AdaptiveAddToWorld.AddToWorldTimeSliceMax`.
3. **Hang-glider flight and entering the maze from above** — deliberately deferred; that is a job for the game, not the editor.
4. **Greedy meshing** — merging coplanar quads. At present every face produces its own quad; at 1.4 million triangles there is something to save. The `IMazeMeshBuilder` interface was introduced with exactly this in mind.
5. **Stacking objects.** Placements are invisible to the geometric rules: an object neither occupies its cell nor holds weight. So two objects fit in one cell, and a crate cannot be put on a crate. The cure is an occupancy map handed to the rules alongside the grid. One consequence to keep in mind in advance: deleting the lower object leaves the upper one unsupported, and the export will report it as skipped — which is exactly right.
6. **The generator's second output** — density, spacing, a per-room cap on the object type. Deliberately deferred until hand placement has been used in anger: rules written before the experience are guesses in code.
7. **A runtime registry of object state** — the thing the identities exist for. So far nobody reads them except the export that hands them out.
