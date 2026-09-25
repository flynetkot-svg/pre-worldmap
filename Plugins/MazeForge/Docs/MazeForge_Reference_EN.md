# MazeForge — Reference

**For:** looking up what a particular field, button or log line means.
**Version:** MazeForge 0.4.0 · Unreal Engine 5.8.2
**How to build a first world:** `MazeForge_QuickStart_EN.md`
**По-русски:** `MazeForge_Reference_RU.md`

In the tables, "Default" is what a freshly created asset has in that field. "Set it?" answers one question: does this have to be filled in for things to work.

- **required** — the corresponding feature does not work without it, and says so in the log;
- **optional** — it works as it is; change it when you need to;
- **leave alone** — the value was chosen, and the reason is given;
- **output** — read-only, written by the plugin.

---

## Contents

1. [The editing mode panel](#1-the-editing-mode-panel)
2. [The maze asset](#2-the-maze-asset-maze-grid)
3. [Build settings](#3-build-settings-maze-build-settings)
4. [The object library](#4-the-object-library-maze-object-library)
5. [The spawn asset](#5-the-spawn-asset-maze-spawn-asset)
6. [Spawn rules](#6-spawn-rules-maze-spawn-rules-asset)
7. [The manifest](#7-the-manifest-maze-world-manifest)
8. [The world graph](#8-the-world-graph-maze-world-graph)
9. [The streaming component](#9-the-streaming-component-maze-streaming)
10. [Streaming rules](#10-streaming-rules-maze-streaming-rules-asset)
11. [The world map](#11-the-world-map)
12. [Enumerations](#12-enumerations)
13. [Log messages](#13-log-messages)
14. [What refers to what](#14-what-refers-to-what)

---

## 1. The editing mode panel

The sections appear top to bottom in this order. The order is set explicitly; otherwise the engine would sort them alphabetically.

### Target

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Target Asset` | empty | **required** | Which maze is being edited. Without it the panel draws nothing and all fourteen buttons refuse with `no Target Asset set`. |
| `Status` | — | output | The state of the target in one line: plane or volume, cells, rooms, meshes, snapshot. |

The chosen target survives an editor restart: the panel is stored in `EditorPerProjectUserSettings.ini`.

### Generate

One button, no fields.

| Button | What it does |
|---|---|
| **Generate Maze** | Fills the grid using the generator set on the maze asset. Snapshots whatever is drawn first. Disabled while the generator is `Manual` — the caption under it says so. |

### Brush

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Tool` | `Cells` | optional | What the mouse draws: the mass of the maze, or objects. A tool rather than a modifier key, because cells go into the grid and objects into the spawn asset, and a key that silently changes which file a click edits is a good way to lose work. |
| `Paint Type` | `Solid` | optional | The cell type. Visible only when `Tool = Cells`. |
| `Brush Size` | `1` | optional | The side of the brush square, 1–16. Visible only when `Tool = Cells`. |
| `Paint Object Type` | empty | output | The selected object type. Set by clicking a swatch in the palette. Visible only when `Tool = Objects`. |
| `Paint Band` | `Play` | **matters** | Which depth band new objects go into. Visible only when `Tool = Objects`. For transition points it must be `Play`. |
| `Paint Variant` | `0` | optional | The surface variant written into the cell. Which variants exist is decided by the palette in the build settings, per cell type. An index the type does not declare is not an error — the cell simply gets the base surface. |
| `Depth Apply` | `Fill Bands` | leave alone | Where a stroke lands in depth. `Fill Bands` follows the flags of the depth profile — the normal mode. The other three (`Active Slice Only`, `All Depth`, `Play Band Only`) are pinpoint edits. |
| `Active Depth Slice` | `0` | optional | The depth slice the cursor is caught on. `PgUp` / `PgDn`. Set for you by `Change Current Maze`. |

| Button | What it does |
|---|---|
| **Fill Back Wall** | Fills the whole maze with back wall, using `Paint Variant`. A starting point, not a final look: the back wall is off by default, painting a 707×376 map by hand is hours, and a forgotten patch is a hole into the skybox that only shows up in game. |
| **Clear Back Wall** | Removes all of it. |

### Objects

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Spawn Rules` | empty | required for `Generate Objects` | How much of what a generated pass scatters. Read by `Generate Objects` and by nothing else. |

Below it is the object palette. With no library, the swatches are replaced by a line saying exactly what is not filled in. Clicking a swatch also switches `Tool` to `Objects`.

| Button | What it does |
|---|---|
| **Clear All Objects** | Removes **every** placement of this maze, including hand-placed ones. Its own button, not part of `Clear All Changes`: the drawing is redrawn many times, the decor pass is made once and slowly. The snapshot does not hold placements, so this is the one irreversible loss in the panel. |
| **Generate Objects** | Scatters objects according to the rules. Replaces what the previous run placed and touches nothing else — running it twice is as safe as running it once. One transaction over the whole pass: `Ctrl+Z` undoes the button, not one crate. |
| **Clear Generated Objects** | Removes only what the generator scattered. Hand-placed objects stay. |

### Display

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Style` | empty | optional | The overlay style preset. **Empty means the built-in values, not "no overlay".** Your own: Miscellaneous → Data Asset → Maze Editor Style Asset. |
| `Show Grid` | on | optional | The grid. |
| `Show Rooms` | on | optional | Room frames. |
| `Show Preview` | on | optional | Preview cubes. |
| `Show Room Preview` | off | optional | Draw the slicing the slicer **would** produce, instead of the one that was stored. |
| `Detach Previous On Target Switch` | on | leave alone | Take the previous maze off the map when the target changes. Another maze's real geometry standing behind this maze's preview cubes is the single most confusing state the mode can be in. Turn it off only to test a door between two mazes in PIE. |
| `Isolate Active Layer` | on | optional | Bring the layer being painted to the front and dim the rest. |
| `Peek Key` | `Q` | optional | Hold to suspend the dimming. Not `Alt`: that is the viewport orbit. |
| `Inactive Layer Dim` | `0.45` | optional | How strongly the other layers are held back. `1` — not at all. |
| `Grid Window Cells` | `48` | leave alone | Fallback size of the grid overlay window. Used only where the visible area cannot be computed. |

### Snapshot

| Button | What it does |
|---|---|
| **Save** | Snapshots the drawing into the asset itself. Survives an editor restart, unlike `Ctrl+Z`. |
| **Restore** | Brings the drawing back. |

The snapshot holds **only the grid**. Object placements are not in it.

### Build

| Button | What it does |
|---|---|
| **Apply Changes** | The whole pipeline: snapshot the plane → grow the volume → slice into rooms → bake meshes → export levels and the manifest → attach the levels to the map. Then `Ctrl+Shift+S`. |
| **Clear All Changes** | Erases the drawing. First detaches the levels, then clears the grid, the slicing and the bake state, then clears the manifest reference. A snapshot is taken automatically and `Restore` brings the drawing back — but not the levels; those come back from `Apply Changes`. The level and mesh assets are left on disk and are overwritten by the next build. |

### Edit

Two buttons for editing a maze that has already been built. Disabled until it has been.

| Button | What it does |
|---|---|
| **Change Current Maze** | Detaches the levels, flattens the volume back to a plane and sets the active slice to the drawing plane. Flattening is exact: a column that had at least one solid cell stays a cell. |
| **Apply Changes To Current Maze** | The same work as `Apply Changes`. The two differ in what they mean, not in what they do: one says "build this", the other "put back what I was editing". |

### World

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `World Graph` | empty | required for `Attach All Mazes` | The same asset the character's streaming component points at. Named here as well because attaching is editor work, done with the map open and the character closed. |

| Button | What it does |
|---|---|
| **Open World Map** | Opens the world map window, handing it the graph from the field above. |
| **Attach All Mazes In World Graph** | Attaches the levels of every maze in the graph to the open map. Nothing is generated, exported or re-baked: this only makes the map agree with the graph. |

### Advanced

Collapsed by default. The same pipeline steps individually, for when you need just one of them.

| Field / button | What it does |
|---|---|
| `Force Full Rebake` | Rebuild every room's mesh instead of only the ones that changed. The bake normally compares each room against a hash of what it is made of. That comparison cannot see meshes on disk being deleted or edited by hand — this is the way out when the two have drifted apart. |
| **Build Depth Volume** | Grow the volume from the plane. |
| **Flatten To Plane** | Flatten it back. Exactly reversible. |
| **Slice Into Rooms** | Slice into rooms. |
| **Build Room Meshes** | Bake the meshes. |
| **Export Rooms To Levels** | Write the levels and the manifest. |
| **Attach Rooms To Level** | Attach this maze's levels to the map. |
| **Detach Rooms From Level** | Take them off. |

---

## 2. The maze asset (Maze Grid)

**Create:** Content Browser → right-click → **MazeForge → Maze Grid**.
**How many:** one per maze.

The source of truth for the whole pipeline. There are deliberately no buttons inside: this is a parameter asset, and the actions are gathered in the mode panel.

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Grid` | empty | **required** | The grid: cell size, extents, depth profile, world borders, origin. |
| `Grid → World Origin` | `(0, 0, 0)` | optional, but see below | Shift of the whole grid in the world. Along Y the `Play` centre is already accounted for; this is an extra offset. |
| `Generator` | created for you | optional | What fills the grid: manual, random, from an image, from a mesh. |
| `Slicer` | created for you | optional | What cuts it into rooms. `Uniform Grid` with a 32×32-cell room by default. |
| `Rooms` | empty | output | The slicing result. |
| `Maze Name` | empty | **required once there are two mazes** | A short name. See below. |
| `Build Settings` | empty | **required** | Palette and build rules. While this is empty the settings asset is not used at all: the export falls back on hard-coded defaults, and edits in the asset apply to nothing. The most common mistake at the start. |
| `Spawns` | empty | required for objects | Where this maze's objects stand. A maze with no decor needs none. |
| `Built Manifest (output)` | empty | output | **The game does not read this.** See below. |
| `Grid Revision` | `0` | output | A coarse "has anything changed anywhere" marker. |
| `Snapshot Info` | empty | output | What is in the snapshot. |

> **`Maze Name`.** Empty is the old behaviour exactly, byte for byte, so an existing maze keeps its assets. Set it and everything this maze produces moves into a compartment of its own: the levels, meshes and manifest go into a subfolder named after it, and the asset names carry it.
>
> It exists because room ids are not unique across mazes — every uniform slicing starts at `R_000_000` — so two mazes sharing one set of build settings write the same level names, the same mesh names and the same manifest. Building the test maze silently overwrote the production one.
>
> Anything that is not a letter, a digit or an underscore is stripped: this becomes a package path segment.

> **About `World Origin`.** Nothing enforces giving mazes distinct coordinates: the old maze is fully unloaded before the new one arrives, so overlapping is not fatal. But distinct origins make "where am I" answerable, and let both be held for a moment if the transition wants a crossfade.

> **`Built Manifest (output)` is a trap.** It is the manifest written by the last export. The game does **not** take it from here. The runtime reads the `Manifest` field on the `Maze Streaming` component of whatever is watching the player, and that field is set by hand, once, and never follows this one.
>
> The row is greyed out like something the system manages for you, and reads as the authoritative answer — while the game goes on loading a manifest that has since been deleted.

---

## 3. Build settings (Maze Build Settings)

**Create:** Miscellaneous → Data Asset → **Maze Build Settings**.
**How many:** one per project, shared by every maze.

It answers "how to build", not "where it lands". The latter is `Maze Name`'s job.

### Output → Packages

| Field | Default | Set it? |
|---|---|---|
| `Level Package Root` | `/Game/MazeForge/Maps` | optional |
| `Mesh Package Root` | `/Game/MazeForge/Meshes` | optional |
| `Manifest Package Root` | empty — next to the levels | leave alone |
| `Manifest Asset Name` | `DA_MazeWorldManifest` | leave alone |

### Output → Outliner

| Field | Default | Meaning |
|---|---|---|
| `Outliner Root Folder` | `Levels` | Root folder for generated actors. Empty — do not arrange anything. |
| `Outliner Mesh Sub Folder` | `MazeMeshes` | Sub-folder for meshes inside the room folder. |
| `Outliner Object Sub Folder` | `MazeObjects` | Sub-folder for spawned objects. |
| `Outliner Folder Per Room` | on | A separate folder per room. Turn it off for small mazes: two hundred folders help, a dozen only adds clicks. |

The arrangement exists for one thing: one eye icon in the Outliner hides all the generated geometry of a room while your decor and the anchor stay visible. Hiding the walls to look at where the crates ended up is something you do constantly.

### Output

| Field | Default | Meaning |
|---|---|---|
| `Generated Tag` | `MazeForge.Generated` | The tag put on generated actors. A re-export deletes **only** the tagged ones and does not touch decor placed in the level. Without it the very first edit of the maze would wipe out the team's work. |
| `Rooms Per Flush` | `16` | After how many rooms to unload finished work from memory. `0` — never. **Do not use `0` on large mazes:** at 208 rooms the editor crashed **after** a successful save — what ran out was not RAM but the RHI's address space for the resources of six hundred meshes. |

### Bake

| Field | Default | Meaning |
|---|---|---|
| `Split By Depth Band` | on | Three meshes per room instead of one: collision only on `Play`, and the foreground can be faded separately. |
| `Merge Materials` | off | Merge materials into an atlas. More expensive in time, cheaper in draw calls. |
| `Cull Enclosed Cells` | on | Throw away cells fully surrounded by solid. The main geometry saving. |
| `Cull Far Boundary Faces` | off | Do not build the far world face along −Y. It is not visible in game and it saves a whole layer of quads per room. But the mesh then stays open, and a free camera in debugging will see a hole. Off by default: correct geometry matters more; turn it on deliberately for the sake of the budget. |

### Palette

A map of "cell type → what to build it with". Filled in by the constructor, so it works out of the box.

Each entry has `Mesh` (empty — the stock cube), `Material Override`, `Editor Color` and a list of `Variants`.

A variant is **not** a new cell type. The geometry is the same; only the surface differs — one stretch of level brick, another rock.

| Variant field | Meaning |
|---|---|
| `Name` | What it is called in the brush hint. |
| `Material` | The variant's material. Empty — the type's base material. |
| `Editor Color` | The swatch colour and the cell colour in the preview. Variants must differ or the layers merge visually. |

An index past the end falls back to the base surface, so deleting a variant never breaks an already painted maze, it only makes it plainer. A room whose cells use two variants bakes into **one mesh with two material slots**.

---

## 4. The object library (Maze Object Library)

**Create:** Miscellaneous → Data Asset → **Maze Object Library**.
**How many:** one per project.

The catalogue of what can be placed at all. A lamp is the same lamp in every maze; where the lamps stand is the spawn asset's business.

The only field is `Types`. Each entry:

### Identity

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Type Id` | empty | **required** | The key placements and rules refer to. Deliberately a name and not an index: dragging a row in the editor would silently repoint every placement in the project at a different object. |
| `Display Name` | empty | optional | The label in the palette. |
| `Category` | `Decor` | optional | Groups the palette and lets a rule say "any decor". |
| `Actor Class` | empty | **required except for `Entry`** | What gets spawned. Soft on purpose: a library of fifty types must not drag fifty blueprints into memory just because the panel was opened. |
| `Editor Color` | orange | optional | The marker colour on the 2D map. Types must differ or the map reads as noise. |

### Placement

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Allowed Anchors` | `Floor` | optional | What the object may hold on by. A mask: a torch can declare both wall and ceiling and let the brush pick whichever fits where you clicked. |
| `Footprint Cells` | `(1, 1)` | optional | How much space it needs **clear**, in cells. X along the level, Y up Z. **Not the size of the mesh.** In cells deliberately: a blueprint that builds its mesh at runtime has no bounds to read, and "does this crate fit in that gap" is needed by the brush, the generator and the export alike. |
| `Snap To Anchor Surface` | on | leave alone | Push the spawned actor until its own edge rests on the surface it is anchored to. Measured from the actor's bounds at export, so it is right for any mesh and any pivot. Turn it off for a prop meant to cross the surface — a lamp on a chain, a pipe sunk into a wall — and place it with `Offset` instead. |
| `Facing Mode` | `Fixed` | optional | How the object decides which way to face. |
| `Fixed Rotation` | zero | optional | The angle for `Fixed`, and the starting point for the other modes. |
| `Offset` | zero | optional | A nudge inside the cell, in units. A lamp hanging a little below the ceiling. |
| `Scale` | `(1, 1, 1)` | optional | The size the actor is spawned at. See below. |
| `Transition Role` | `None` | required for transitions | `Gate` or `Entry`. |

> **Why `Scale` lives here.** The export owns every actor it makes: each `Apply Changes` destroys them and builds them again from the type. A size typed into the level survives exactly until the next `Apply`. Set here, it comes back every time.
>
> It scales the actor, **not the space it claims**: `FootprintCells` is declared, not measured. An object grown well past its footprint will happily overlap its neighbours. Grow the footprint with it when the object is meant to keep them away.

> **Careful with the `Free` anchor.** It fits anywhere empty and so effectively turns the check off. It also puts the object in the centre of the cell rather than on an edge, so the same crate ends up half a cell above its floor-standing siblings.

> **The `Floor` anchor and the `Floor` cell type are two different things with one name.** The anchor does not look at the cell type at all: it asks whether there is **mass** under the bottom edge of the footprint, and both `Solid` and `Floor` count as mass. A maze drawn entirely out of `Solid` is perfect ground for floor-anchored objects — the fact that the generator never paints `Floor` cells has no bearing on it.

> **The world border counts as support too.** It is painted into no cell and does not depend on depth — `IsBorderCell` looks only at X and Z — but the export builds it as real mass. So an object may legitimately stand on the bottom border of the map. It cannot be placed inside the border: those cells are impassable, and the brush says so in a line of its own.

---

## 5. The spawn asset (Maze Spawn Asset)

**Create:** Miscellaneous → Data Asset → **Maze Spawn Asset**.
**How many:** one per maze. Referenced from `Maze Grid → Build → Spawns`.

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Library` | empty | **required** | Which library describes the types these placements refer to. |
| `Placements` | empty | output | The placements themselves. Filled by the brush and the generator. |
| `Next Id` | `1` | output | The next number to hand out. Only ever goes up. |

A separate asset from the grid because of lifetime, not tidiness. The maze is generated, cleared, restored and redrawn many times; the decor pass is later and far more expensive. The practical consequence: **`Clear All Changes` erases the drawing and leaves the objects alone**, and **the snapshot holds only the drawing**.

> **About the numbers.** `Next Id` is a counter, not the array length: deleting a placement does not renumber the others, and a number that has been used never comes back. The numbers key a runtime registry of what the player has done to each object, so a reused number is a save pointing at the wrong thing.
>
> A number is handed out at the **first export**, not when you draw. A placement the export refused to build gets none — otherwise "has an id" would stop meaning "exists in the world" the first time a wall was redrawn under a crate.

---

## 6. Spawn rules (Maze Spawn Rules Asset)

**Create:** Miscellaneous → Data Asset → **Maze Spawn Rules Asset**.
**How many:** one per project. How thickly crates are strewn is a decision about the game, and ten mazes should share one answer and change it in one place.

| Field | Default | Meaning |
|---|---|---|
| `Rules` | empty | The list of rules. |
| `Seed` | `1337` | What makes a run repeatable. Two runs over the same maze with the same seed produce the same furniture, so a layout can be judged, adjusted and judged again. |

### One rule

| Field | Default | Meaning |
|---|---|---|
| `Target` | `Type` | The rule names one type or a whole category. |
| `Type Id` | empty | Visible only when `Target = Type`. Must match a `Type Id` in the library exactly. |
| `Category` | `Decor` | Visible only when `Target = Category`. Every type of that category is fair game. |
| `Rooms` | wide open | The room filter, below. Left alone, it is every room. |
| `Enabled` | on | Switch a rule off without deleting it. |
| `Min Per Room` | `1` | Fewest per room. A room with nowhere to put them gets fewer and says so. |
| `Max Per Room` | `3` | Most per room. Below the minimum it is treated as the minimum. |
| `Min Spacing Cells` | `2` | How many empty cells to keep from anything already placed. Not a correctness rule — zero spacing still refuses to overlap. It is what stops a room looking poured rather than furnished. |
| `Band` | `Play` | Which band. |

> **Why the type is drawn per object under `Target = Category`, not per rule.** "Three decor in this room" should come out a crate, a barrel and a wardrobe. Drawn once per rule it would be three wardrobes — exactly the arrangement one type-named rule would have given.

### The room filter

| Field | Default | Meaning |
|---|---|---|
| `Min Exits` | `0` | Fewest exits the room must have. **`0` means "no limit", not "zero exits".** A dead end has one. |
| `Max Exits` | `0` | Most it may have. `0` — no limit. |
| `Transitions` | `Any` | `Only` — rooms holding a transition point; `Never` — rooms holding none. |

> **Why the filter reads properties instead of tags on rooms.** Rooms are produced by the slicer: change the room size and every room is a new room with a new id. Anything typed onto a room by hand would die at the next re-slicing, and quietly: the tag would simply be gone and the rule would go on matching nothing. The number of exits and whether a transition stands in the room are recomputed from the maze itself and survive re-slicing.
>
> It buys the sentences worth saying: enemies only where there is more than one way out, supplies in dead ends, nothing loose in the room you arrive into.

### Band and anchor

A rule's band decides more than depth: the anchor looks for mass **in that same band**.

| Band | Filled by default | Will the `Floor` anchor find support |
|---|---|---|
| `Background` | yes | yes |
| `Play` | yes | yes |
| `Foreground` | **no** | **no** |

An unfilled band gives `objects 0 placed` and `N rooms short of their minimum` — formally true and completely opaque. The brush names the real reason: set `Paint Band` to the same band and hover over a cell above a floor, and it reads `no mass in the row below`.

Filling is switched on in `Maze Grid → Grid → Depth → Fill`, and needs an `Apply Changes` afterwards: the checkbox does not change an already built grid, the volume is grown by the button.

### What the generator will not do

- **It does not touch hand-placed objects.** They are ground that is already taken.
- **It never places transition points**, even when a rule about the `System` category covers them. A transition point has an identity: its number is what the world graph links mazes by, and generated placements are destroyed and remade on every run. A generated door would come back each time with a new number, and the link in the graph would die without a word.

---

## 7. The manifest (Maze World Manifest)

**Never created by hand.** Written by the export, one per maze, next to its levels.

The boundary between the editor and the game: the runtime sees only the manifest and knows nothing about the voxel grid — so neither the megabytes of cells nor the editor code end up in the shipping build.

Everything inside is read-only:

| Field | Meaning |
|---|---|
| `Rooms` | The rooms: id, level reference, bounds, neighbours, triangle estimate. |
| `World Bounds` | The bounds of the whole maze. |
| `Play Plane Y` | World Y of the plane the player moves in. Always the centre of the `Play` band. |
| `Transitions` | Both ends of every transition: placement id, role, location, rotation, cell. |

---

## 8. The world graph (Maze World Graph)

**Create:** Miscellaneous → Data Asset → **Maze World Graph**.
**How many:** one per project.

The thing it exists to prevent: **a door that knows its own destination.** Bake the destination into the door and you need one door blueprint per doorway, the topology of the game lives scattered across blueprint defaults, and the question "is there a way out of TEST5 at all" has no way of being asked.

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Mazes` | empty | **required** | The mazes of this world, by manifest. The manifest and nothing else: it already carries everything both halves need. |
| `Links` | empty | drawn on the map | The links. |
| `Layout` | empty | output | Where mazes have been dragged on the map. Editor furniture; it changes nothing about how the game runs. |

### One link

| Field | Meaning |
|---|---|
| `From Maze` / `From Id` | The manifest and the placement id of the `Gate` the link starts at. |
| `To Maze` / `To Id` | The manifest and the placement id of the `Entry` it arrives at. |

> **A link is one-way, deliberately.** A door you can walk through both ways is two links, because that is what it is: two gates and two entries, and the way back does not have to arrive where the way there set off. A single two-way object would hide that, and hide it exactly where it matters.

> **Links are keyed by placement id, not by name, and that has a sharp edge.** Delete a transition point and draw it again and it is a **different** point with a new number, so the link that named the old one is dead. That is honest — it really is a different door — but it means dead links are shown loudly rather than quietly ignored.

---

## 9. The streaming component (Maze Streaming)

**Add:** to the character blueprint, **Add Component → Maze Streaming**.

The observer the room pool follows. The references to the manifest and the rules live here rather than in the project settings: this way the designer picks the maze and the streaming profile right in the character blueprint, and several sets can be kept side by side for comparison.

### MazeForge

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Manifest` | empty | **required** | Which maze to stream. Without it an `Error` goes to the log and nothing streams. |
| `Rules` | empty | **required** | The streaming budgets. Without them, also an `Error`. |
| `World Graph` | empty | required once there are two mazes | What a door asks when it is walked into. Leave it empty for a single-maze game — nothing else reads it. |
| `Drive Camera Bounds` | on | optional | Widen the horizontal bounds of the side-view camera to the size of the maze. |
| `Maze Ready Timeout Seconds` | `5.0` | leave alone | How long to wait for the arrival room before firing `OnMazeReady` anyway, with an error. A safety catch, not a feature: whatever holds a black screen on `OnMazeReady` has no other way out, so a room that never comes up would hang the game with nothing on screen and nothing in the log. `0` disables the catch. |

### MazeForge → Transition

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Handle Transition Fade` | on | leave alone | Do the fade here rather than in the game. The two halves of a fade have to agree about colour, alpha and which one holds — and when they are two nodes in two blueprints, nothing checks that they still do. A cleared checkbox in a third asset is enough to make a transition blink, and neither the compiler nor the log says a word. Turn it off only to present the transition yourself: the events still fire, and the ordering and timing stay here. |
| `Transition Fade Out Seconds` | `0.25` | optional | How long the world stays visible after a door is taken, before the observer moves. **Nothing moves until this has elapsed.** The old order had the teleport inside the call while the fade only started, so for a quarter of a second the player watched the next maze flick past and the camera set off after him. |
| `Transition Fade In Seconds` | `0.3` | optional | How long coming back takes. Appearance only. |
| `Transition Fade Color` | black | optional | What the screen fades to. Appearance only. |
| `Minimum Transition Seconds` | `0.6` | optional | The shortest a whole transition is allowed to take, measured **from the door**. The destination room is often already loaded — every sublevel attached to the map arrives in PIE loaded — and is then up in the same frame as the switch. Without a floor under it the screen blinks, which reads as a fault rather than as a door. `0` lets a transition be as fast as the streaming allows. |

### What Blueprints get

| Node | What it does |
|---|---|
| **Take Transition For** (`Traveller`, `Gate`) | The whole of a door in one node: who walked in, and which door it was. `Gate` defaults to `Self`. Everything else it works out: the placement id off the gate's own component, the streaming component off the traveller. It exists because the three-node version had two places to wire the wrong pin, and both failed **silently**. This one says something on every path it can fail on. |
| **Take Transition** (`Placement Id`) | The same, with the number supplied by you. |
| **Switch To Maze At Transition** | Switch to a maze, arriving at one of its transition points. Useful for a scripted jump that is not a door. |
| **Switch To Maze At Location** | Switch to a maze, arriving at a world coordinate. |
| **Is Current Room Loaded** | True when the room the observer stands in is loaded and visible. |
| **On Transition Started** | Fires the moment a door is taken, before anything has moved. Bind a fade-out, a cutscene or a summary screen here. |
| **On Maze Ready** | Fires when the arrival room is loaded and visible. The world is real again; show it. |

The pair `On Transition Started` / `On Maze Ready` is the whole transition: hide the world here, show it again there.

### The order of a transition

1. The door calls `Take Transition For`.
2. The component refuses if a transition is already under way. A door firing twice is the normal case: a trigger volume sees the capsule and the mesh, and two gates can overlap.
3. The transition clock starts — **at the door**, not at the move.
4. `On Transition Started` fires; the fade-out begins.
5. Wait `Transition Fade Out Seconds`. **Nothing moves.**
6. The manifest switches and the observer is moved. The camera is cut in the same frame.
7. Wait until the room under the observer is loaded **and visible** — a level that is loaded but not yet added to the world is geometry the player would fall through.
8. And until `Minimum Transition Seconds` have passed since the door.
9. Fade in, then `On Maze Ready`.
10. If the room never comes up within `Maze Ready Timeout Seconds`, the same happens with an `Error` in the log. Better a visibly broken arrival than a dead screen.

---

## 10. Streaming rules (Maze Streaming Rules Asset)

**Create:** Miscellaneous → Data Asset → **Maze Streaming Rules Asset**.
**How many:** one per project. Nothing inside needs changing: the rule set is created by the constructor.

### Budget

| Field | Default | Meaning |
|---|---|---|
| `Load Threshold` | `0.35` | Above this, start loading. |
| `Unload Threshold` | `0.15` | Below this, unload. Two thresholds are the hysteresis: with one, a room on the border would load and unload every frame. |
| `Max Loaded Rooms` | `9` | How many rooms to hold at once. |
| `Max Concurrent Loads` | `2` | How many loads run at once. More means a sharper hitch on disk. |
| `Min Time Loaded Seconds` | `2.0` | The minimum lifetime of a loaded room. The second safeguard against flicker. |
| `Update Interval Seconds` | `0.1` | How often to recompute priorities. 10 Hz is enough and does not heat the profiler. |

### The rules

Each writes its own "desire" to keep a room loaded. The **strongest** wins, not the sum — otherwise noise would add up into a decision. Each has a `Weight` (default `1.0`) and `Enabled`.

| Rule | What it considers | Fields |
|---|---|---|
| `Camera Frame` | being inside the camera frame | `Margin UU` = 800, `Falloff UU` = 4000 |
| `Portal Graph` | neighbours by passability, not by straight line | `Max Depth` = 3 |
| `Velocity Predict` | where the player is heading | `Look Ahead Seconds` = 1.5, `Max Look Ahead Seconds` = 3.0, `Reference Speed` = 600, `Falloff UU` = 4000 |
| `Vertical Motion` | falling down a shaft, climbing a ladder | `Max Fall Probe UU` = 6000, `Climb Probe UU` = 3000, `Falloff UU` = 3000 |
| `Player Radius` | plain proximity — a safety net | `Radius UU` = 6000 |

---

## 11. The world map

Opened from the mode panel: `World` → **Open World Map**. The graph is handed over from `World → World Graph`; it can also be picked in the window itself.

Mazes are drawn as schematics from their manifests: room rectangles, **blue** squares for gates, **red** for entries, each with its number beside it.

| Action | How |
|---|---|
| Add a maze | the `Add maze` picker at the top |
| Link | click a blue square, then a red one |
| Cancel a pending link | `Escape` |
| Select a link | click the line |
| Delete a link | select it and press `Delete` |
| Move a maze | drag its rectangle |
| Zoom | wheel |
| Pan | middle or right drag |
| Frame everything | the `Fit` button or `F` |
| Clear dead links | the `Remove N dead links` button |

The status line at the bottom always says what is going on:

| Line | Meaning |
|---|---|
| `Pick a Maze World Graph above.` | no graph chosen |
| `This world graph names no mazes.` | the graph holds no manifests |
| `N mazes are named, but none of them has anything to draw.` | the manifests are there, but the mazes have never been built |
| `Gate N is waiting — click a red square...` | waiting for the second click |
| `Link N → M selected. Delete removes it.` | a link is selected |
| `N of M links lead nowhere...` | there are dead links |
| `N mazes, M links, 100%.` | all well |

`Remove N dead links` removes links whose ends no longer exist. A link into a maze that is not loaded is left alone: not being able to see it is not the same as it being gone.

The layout of the mazes is stored in the graph and changes nothing about the game: a world with every maze piled in one corner plays exactly like a tidy one.

---

## 12. Enumerations

### Cell type

| Value | Meaning |
|---|---|
| `Empty` | Emptiness — passable space. |
| `Solid` | Wall, the mass of the maze. |
| `Floor` | A slab between floors. Geometrically the same as `Solid`, split out for decor. |
| `BackWall` | Painted back wall: the surface that closes the maze off from behind. It produces geometry but is **not** solid: it lives in its own slice at the far edge of the depth and the player never reaches it. A window is simply a cell where the back wall was not painted — through the hole you see the sky. |

### Depth band

| Value | Meaning |
|---|---|
| `Background` | Far plane: decor, no collision. |
| `Play` | The play band: all geometry and all collision. The player is locked in its centre. |
| `Foreground` | Near plane: decor in front of the player, no collision. |

### Anchor

Where an object touches the mass of the maze. A mask on the type (several allowed), a single value on the placement.

| Value | Meaning |
|---|---|
| `Floor` | The bottom edge rests on mass. Crates, enemies, anything that stands. |
| `Ceiling` | The top edge touches mass. Lamps, hanging signs. |
| `Wall` | A side edge touches mass. Torches, pictures, levers. |
| `Free` | Touches nothing. Floating decor, flying enemies, trigger volumes. |

The brush picks the first that fits, in the order `Floor → Ceiling → Wall → Free`.

### Object category

| Value | Meaning |
|---|---|
| `Climb` | Ladders, ropes, climbable rock. |
| `Decor` | Crates, barrels, wardrobes, antennae — anything that is only looked at. |
| `Item` | Ammo, weapons, quest items, traps. |
| `Enemy` | Enemies and their spawn points. |
| `System` | Teleports, save points, triggers. |

### Facing mode

| Value | Meaning |
|---|---|
| `Fixed` | Always the angle set on the type. |
| `Random X` | Left or right along X, chosen by the seed. |
| `Away From Wall` | Turned away from the mass it is attached to. A mode and not a checkbox: a torch on a wall has to face away from it, and nobody wants to verify that by hand across a hundred torches. |

### Transition role

| Value | Meaning |
|---|---|
| `None` | An ordinary object. |
| `Entry` | Where the player arrives — the red square. A coordinate and not a thing, so the type normally has **no** `Actor Class` and nothing is spawned for it. Give it the `Free` anchor: an arrival point is usually a spot in mid-air, and every other anchor would refuse it. |
| `Gate` | Where the player leaves — the blue square. This one **is** a thing: the door he walks into, so the type needs an `Actor Class`. That actor reads its own placement id off its id component and asks the world graph where it leads. It never holds a destination itself, which is why one `BP_Door` serves every door in the game. |

Two roles and no names. The world graph keys a transition by its placement id, which every placement already has, is unique for the life of the project and is never handed out twice. A name would be a second identity to keep in step with the first, and the first is the one the runtime can actually read off a spawned actor.

---

## 13. Log messages

Filter the Output Log by `MazeForge`.

### A healthy start

```
Streaming: <character> reads manifest <path>
Streaming: now reading manifest <path> (was none).
Camera bounds set from the manifest: X 0..25600
```

### A healthy transition

```
Streaming: transition started; moving in 0.25 s.
Streaming: switching to manifest <path>, entry at X=... Y=... Z=...
Streaming: now reading manifest <new> (was <old>).
Streaming: room R_000_000 (<package>) is up — the maze is ready, 0.62 s after the door.
```

### Nothing streams

| Message | Cause |
|---|---|
| `has no manifest ... Nothing will stream.` | the `Manifest` field on the component is empty or points at a deleted asset |
| `has no streaming rules ... Nothing will stream.` | the same for `Rules` |
| `manifest ... lists no rooms` | nearly always a leftover from a build under a different `Maze Name`: every maze writes its own manifest next to its own levels |
| `room ... does not exist on disk` | the manifest is asking for a maze that was never built under this name — almost always it is a **different** maze's manifest |
| `room ... exists but is not attached to the persistent level` | open the map, MazeForge mode, `Attach Rooms To Level`, save the map |
| `room ... has its eye closed in the Levels panel` | a level with a closed eye will not load in PIE |
| `the player entered unloaded room ...` | the player reached a room the pool should have had ready. If it happens while running, raise `Max Loaded Rooms` or the look-ahead |

### The door

| Message | Cause |
|---|---|
| (silence) | the blueprint's execution chain does not reach the node. An unfinished chain compiles without warnings |
| `Take Transition For was given no gate.` | the `Gate` pin was unwired; put `Self` back |
| `carries no placement id` | the type has no `Transition Role = Gate`, or the maze has not been built since the point was placed |
| `has no MazeForge Streaming component` | something other than the character walked in. Use `OverlapOnlyPawn` |
| `has no World Graph` | the `World Graph` field on the component is empty |
| `does not know which maze it is in` | the `Manifest` field on the component is empty |
| `gate N of ... leads nowhere` | the link was never drawn — or the point was re-placed and took a new number |
| `a transition is already under way; this one is ignored` | normal. The volume caught both the capsule and the mesh |
| `room ... did not come up within N s` | check that the room exists in that manifest and that its level is attached to the map |

### Building

| Message | Cause |
|---|---|
| `entry N ... is in the Background band, not Play` | the arrival point is in the background. The player never goes there. Erase it and place it again with `Paint Band = Play` |
| `gate N ... is in the Background band, not Play` | the same for the door: it cannot be walked into |
| `'X' in room R ... had been resized by hand` | the size was stretched in the level. The rebuild puts it back to what the type asks for. Set `Scale` on the type |
| `... asks to face away from a wall, but its anchor is Floor` | `Away From Wall` on an object that is not against a wall. It is spawned, but the mode does nothing |
| `... lost the anchor it was placed on and took another` | the geometry under the object was redrawn. Worth a look at which way it now faces |
| `... does not fit — <reason>. Not spawned.` | the object has nowhere left to stand |
| `the object at X.. Z.. is of type '...', which is not in the library` | a `Type Id` was renamed after the objects were placed |
| `room ... holds N objects but no geometry` | no geometry means no level, and no level means nowhere to put the objects |
| `UWorld::DestroyActor: World has no context!` | **not your problem.** A false positive from the engine about attached sublevels |

### The placement generator

| Message | Cause |
|---|---|
| `rule ... matches nothing in library ...` | the rule names a type the library does not have |
| `rule ... covers N transition type(s). They were left out` | a rule about the `System` category swept up transition points. They are skipped deliberately |
| `no room in ... passes the filter of rule ...` | the room filter matched nothing. Either it contradicts itself or the maze has no rooms of that shape |
| `rule ... wants at least N exits and at most M` | a self-contradicting filter. The rule will never place anything |
| `room ... fits only N of the M ... its rule asks for` | not an error: a corridor one cell high is a rule meeting a maze |

---

## 14. What refers to what

```
Maze Grid ──→ Build Settings          required
          ──→ Spawns ──→ Object Library    for objects
          ··→ Built Manifest (output; the game does NOT read it)

Spawn Rules ─ ─ ─ by type name → Object Library
                  (no pointer: rules name types as strings)

World Graph ──→ manifest of maze A
            ──→ manifest of maze B
            ──→ Links: (manifest + gate id) → (manifest + entry id)

Character component ──→ Manifest        set by hand
                    ──→ Streaming Rules  set by hand
                    ──→ World Graph      set by hand
```

**Two by-name links the editor does not check:**

- `Placement → Type Id` in the library;
- `Spawn Rule → Type Id` in the library.

The rules asset holds no pointer to a library at all: it names types as strings, and the pairing of rules with a library is established by whatever runs the pass. That is exactly why the library checks its own ids for duplicates and blanks and reports them in the log on every load and every edit: a duplicate means half the placements resolve to the wrong object, and nothing says which half.

**Creation order (leaves first):**

```
1. Maze Build Settings        shared
2. Maze Streaming Rules       shared
3. Maze Object Library        shared
4. Maze Spawn Rules           shared
5. Maze Spawn Asset           one per maze
6. Maze Grid                  one per maze
7. Maze World Graph           shared, after the mazes have been built once
```
