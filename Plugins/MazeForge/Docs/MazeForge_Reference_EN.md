# MazeForge — Reference

**For:** looking up what a particular field, button or log line means.
**Version:** MazeForge 0.5.0 · Unreal Engine 5.8.2
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
15. [Generators](#15-generators)
16. [Reachability, paths, doors and keys](#16-reachability-paths-doors-and-keys)
17. [Ladders](#17-ladders)
18. [Lifts](#18-lifts)
19. [Ropes](#19-ropes)

---

## 1. The editing mode panel

The sections appear top to bottom in this order. The order is set explicitly; otherwise the engine would sort them alphabetically.

### Target

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Target Asset` | empty | **required** | Which maze is being edited. Without it the panel draws nothing and every button refuses with `no Target Asset set`. |
| `Status` | — | output | The state of the target in one line: plane or volume, cells, rooms, meshes, snapshot. |

The chosen target survives an editor restart: the panel is stored in `EditorPerProjectUserSettings.ini`.

### Generate

| Field / button | What it does |
|---|---|
| caption | Which generator the target uses, or that it is drawn by hand. |
| **Generate Maze** | Fills the grid using the generator set on the maze asset. Snapshots whatever is drawn first. Disabled while the generator is `Manual` — the caption says so. |
| `Seed` | The generator's seed, edited right here. The same seed always gives the same maze. |
| **Reroll** | A new random seed and a fresh maze in one press. |

### Maze Features

The target's `Generator` — its class picker and every one of its settings — shown right in the panel, so the maze asset does not have to be opened. The generator's fields are in [15. Generators](#15-generators).

| Field / button | Default | Set it? | Meaning |
|---|---|---|---|
| `Regenerate On Change` | on | optional | Generate the maze again as soon as a generator setting is committed — a slider when it is let go, not on every step. Every run snapshots the drawing first. Off — only `Generate Maze` generates. |
| `Target Dead Ends` | `0` | optional | What `Find Seed` aims for: this many dead ends. `0` — not a target. |
| `Target Min Branch Depth` | `0` | optional | The average branch depth, in grid cells, to reach at least. |
| `Target Nesting` | `0` | optional | Levels of branch-off-a-branch. |
| `Target Min Confusion` | `50` | optional | The confusion score (0–100) to reach at least. |
| `Seed Tries` | `20` | optional | How many seeds `Find Seed` tries. |
| **Find Seed** | — | — | Generates the maze with `Seed Tries` seeds, analyses each the way `Analyze Paths` does, and keeps the one closest to the targets. Any generator. One undo step back to before the search. |
| **Clear Path Plan** | — | — | Removes the main path and branches planned with the `Path` tool. The next generation is random again. |

### Reachability

Can the player get everywhere from where he arrives, and what is the maze like as he walks it. In detail: [16](#16-reachability-paths-doors-and-keys).

| Field / button | Default | Set it? | Meaning |
|---|---|---|---|
| `Require Ladders` | off | optional | Off — every shaft he fits in counts as a way up, on the understanding that a ladder or a lift will stand there; what is reported is the pieces nothing could lead into. On — he walks, falls, steps or jumps up, and climbs only where a `Climb` object stands. |
| `Agent Width Cm` | `84` | optional | How wide the player is. A slit narrower than this is a wall. |
| `Step Up Cm` | `45` | optional | How high he steps up without jumping. `Require Ladders` only. |
| `Jump Height Cm` | `0` | optional | How high a jump gets him. `0` — the check assumes he does not jump. `Require Ladders` only. |
| `Fix Ladder Type` | empty | optional | The ladder `Fix Reachability` stands in a shaft that has to be climbed. Empty — the Classic generator's `Ladder Type`. Also the `Ladder` tool's second choice. |
| `Fix Shaft Width Cells` | `2` | optional | Width of the shafts `Fix Reachability` cuts. |
| `Reachability Status` | — | output | What the last check found. |
| **Check Reachability** | — | — | Hatches the rooms nobody can reach red and the rooms reached in part orange; frames in red the doors and keys something is wrong with. |
| **Fix Reachability** | — | — | Joins every unreachable piece to the rest: a tunnel along the floor and a shaft, with a ladder where the way leads up. One undo step. |
| **Analyze Paths** | — | — | The main path from the start to the exit, the branches, dead ends, loops and a confusion score, drawn over the maze. |
| `Path Status` | — | output | What the last analysis found. |

### Locks

Doors and keys: library types with `Lock Role` `Door` or `Key`, paired by the `Lock Id` on the placement. In detail: [16](#16-reachability-paths-doors-and-keys).

| Field / button | Default | Meaning |
|---|---|---|
| `Lock Count` | `2` | How many doors `Generate Locks` places, each with its key. |
| `Lock Door Type`, `Lock Key Type` | empty | The library types. Empty — the first type with that `Lock Role`. |
| `Lock Ids` | `Red, Blue, Yellow` | Handed out in turn. More locks than names — a colour opens more than one door. |
| `Min Rooms Between Key And Door` | `1` | Rooms that must lie between a door and its key. `1` — never in the same room or in neighbouring ones, so the two are never loaded together around the player. |
| `Max Rooms Between Key And Door` | `0` | The most rooms between them. `0` — no limit. |
| `Max Lock Nesting` | `3` | Most doors in a row a key may hide behind. `1` — keys lie in the open. |
| `Max Keys Held` | `3` | Most keys that come into reach at once, before any of their doors can be opened. |
| `Max Locked Share` | `0.6` | The most of the maze one door may close off, as a share of the places to stand. |
| `Min Locked Cells` | `20` | A door is worth placing only if it closes off at least this many places to stand. |
| `Lock Seed` | `1` | Same seed, same maze — same doors and keys. |
| **Generate Locks** | — | Places the doors and keys. Doors and keys placed by hand stay and are taken into account; the ones from the previous run are replaced. |
| **Clear Generated Locks** | — | Removes only what `Generate Locks` placed. |

### Brush

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Tool` | `Cells` | optional | What the mouse does: paints the mass of the maze (`Cells`), places objects (`Objects`), merges rooms (`Rooms`), plans the Classic generator's paths (`Path`), stands ladders (`Ladder`) or marks rock to be chipped (`Rough`). A tool rather than a modifier key, because cells go into the grid and objects into the spawn asset, and a key that silently changes which file a click edits is a good way to lose work. |
| `Paint Type` | `Solid` | optional | The cell type. Visible only when `Tool = Cells`. |
| `Brush Size` | `1` | optional | The side of the brush square, 1–16. Visible only when `Tool = Cells`. |
| `Auto Floor Surfaces` | on | optional | After each stroke, retype the mass round it: a reachable top the stroke laid bare — a hole cut, a step drawn — becomes `Floor`, a floor it buried `Solid`. Part of the stroke's undo. Not for back wall strokes. Visible only when `Tool = Cells`. |
| `Paint Object Type` | empty | output | The selected object type. Set by clicking a swatch in the palette: a `Climb` type picks the `Ladder` tool, any other the `Objects` tool. Visible with `Tool = Objects` or `Ladder`. |
| `Paint Lock Id` | `Red` | optional | The `Lock Id` given to doors and keys placed with the brush: one red key opens every red door. Ignored for other objects. Visible only when `Tool = Objects`. |
| `Paint Band` | `Play` | **matters** | Which depth band new objects go into. Visible only when `Tool = Objects`. For transition points it must be `Play`. |
| `Paint Variant` | `0` | optional | The surface variant written into the cell. Which variants exist is decided by the palette in the build settings, per cell type. An index the type does not declare is not an error — the cell simply gets the base surface. |
| `Depth Apply` | `Fill Bands` | leave alone | Where a stroke lands in depth. `Fill Bands` follows the flags of the depth profile — the normal mode. The other three (`Active Slice Only`, `All Depth`, `Play Band Only`) are pinpoint edits. |
| `Active Depth Slice` | `0` | optional | The depth slice the cursor is caught on. `PgUp` / `PgDn`. Set for you by `Change Current Maze`. |

| Button | What it does |
|---|---|
| **Fill Back Wall** | Fills the whole maze with back wall, using `Paint Variant`. A starting point, not a final look: the back wall is off by default, painting a 707×376 map by hand is hours, and a forgotten patch is a hole into the skybox that only shows up in game. |
| **Clear Back Wall** | Removes all of it. |
| **Mark Floor Surfaces** | For any maze, drawn or generated: every horizontal surface the player can get onto becomes `Floor`, the rest of the mass `Solid` — the same rule as the generator's `Mark Floor Surfaces`. One undo step. |
| **Place All Ladders** | A ladder in every hole of the maze that has none yet, from the floor to the first exit above. See [17](#17-ladders). |
| **Clear All Ladders** | Removes every ladder and ladder platform, hand-placed ones included. One undo step. |
| **Save Ladder Map** | Saves every ladder and platform now placed into the maze's ladder map, replacing the previous one. |
| **Load Ladder Map** | Replaces the ladders now placed with the saved ladder map. One undo step. |
| **Clear Rough Mask** | Removes the whole `Rough` mask: every face straight again at the next `Apply Changes`. One undo step. |

**The `Rooms` tool** — the room layout on top of the `Uniform Grid` slicer's lattice.

| Action | What it does |
|---|---|
| drag with LMB | Every lattice cell in the rectangle becomes one room. A new merge absorbs the ones it overlaps; neighbours stay separate. |
| `Shift` + drag | Split: remove the merges in the rectangle. |

Merged rooms are hatched, each in its own colour — only in this tool and only until `Apply Changes`. The status line shows `merges N` and, while the layout is not applied, `N ROOMS AFTER APPLY`. A merged room is named after its bottom-left cell; if that name is new, the log lists the rooms that will be retired. Each drag is one transaction; `Ctrl+Z` undoes it whole.

**The `Path` tool** — plans the main path and the branches for the Classic generator's `Path First` style. The plan is drawn yellow (main path) and cyan (branches); with `Regenerate On Change` the maze follows each click.

| Action | What it does |
|---|---|
| LMB | A point on the main path. |
| `Shift` + LMB | A point on the last branch. |
| `Ctrl` + `Shift` + LMB | A new branch. |
| `Ctrl` + LMB | Removes the nearest point. |

**The `Ladder` tool** — stands ladders, two clicks each. Which ladder: the `Climb` type picked in the palette, else `Fix Ladder Type`, else the Classic generator's `Ladder Type`. In detail: [17](#17-ladders).

| Action | What it does |
|---|---|
| first LMB | The bottom. Snaps to the top of a ladder already standing within about a metre — and takes its column, so ladders stack exactly; else to the nearest floor or exit of the column. |
| second LMB | The top. Snaps to the nearest exit within about a metre, else to the highest exit below the cursor, else to the cursor itself, kept under the ceiling. One ladder the whole length, a platform on top. |
| `Shift` + LMB | Cancels a started ladder; otherwise removes every ladder piece in the column under the cursor. |

Before the first click the cell the bottom snaps to is green and the exits of the column amber; after it the ladder to be is drawn whole, with its rungs. The status line gives the height in centimetres and the value of `Climb Height Property`.

**The `Rough` tool** — marks rock to be chipped: the faces of mass in the painted cells, or facing them, come out hewn — flat chips with sharp edges, as a pick leaves rock — at the next `Apply Changes`. Visual only: collision, reachability and placement keep to the cells. The mask is painted on air and mass alike and lives with the drawing (snapshot, `Ctrl+Z`), but a generation leaves it alone.

| Action | What it does |
|---|---|
| LMB / drag | Marks cells, `Brush Size` wide. |
| `Ctrl` + drag | A rectangle. |
| `Shift` + LMB | Erases the mark. |

`Depth Apply` says which bands: `Fill Bands` and `All Depth` — the whole depth, `Play Band Only` — the play band, `Active Slice Only` — one slice. For the face the camera looks at, the play band is enough. The mask is hatched in violet while the tool is in hand; the chipping itself is in the baked meshes, not in the preview cubes. The top of a `Floor` cell — the walkway — and the back wall stay flat. Where the mask ends, the chips fade to the straight face, so the two meet without a gap; a strip one cell wide therefore barely moves — paint a few cells at least.

### Objects

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Spawn Rules` | empty | required for `Generate Objects` | How much of what a generated pass scatters. Read by `Generate Objects` and by nothing else. |

Below it is the object palette. With no library, the swatches are replaced by a line saying exactly what is not filled in. Clicking a swatch also switches `Tool` to `Objects` — or to `Ladder` for a `Climb` type.

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

| Button | What it does |
|---|---|
| **Show / Hide Room Levels** | Hide or show this maze's built levels. Handy while colouring rooms: you see the lattice and the hatching without geometry. The levels stay attached. |

The room hatching is styled in the `Style` preset: `Room Hatch Spacing Cells` (density), `Room Hatch Crossed` (cross-hatch), `Room Hatch Opacity` (opacity of hatching and fill), `Room Merge Palette` (merge colours).

### Snapshot

| Button | What it does |
|---|---|
| **Save** | Snapshots the drawing into the asset itself. Survives an editor restart, unlike `Ctrl+Z`. |
| **Restore** | Brings the drawing back. |

The snapshot holds **only the grid**. Object placements are not in it.

### Build

| Button | What it does |
|---|---|
| **Apply Changes** | The whole pipeline: snapshot the plane → grow the volume → slice into rooms → bake meshes → export levels and the manifest → attach the levels to the map. Rebuilds only what changed: meshes for rooms whose fingerprint moved, levels for rooms whose meshes, neighbours, objects or object types changed. Other levels are not loaded or rewritten. Rooms no longer in the slicing are taken off the map together with their Outliner folders; their files stay. Then `Ctrl+Shift+S`. |
| **Move Stale Rooms To Deprecated** | Moves the levels and meshes of rooms no longer in the slicing into a `Deprecated` folder next to the live ones. Asks first, with the counts. In batches of `Rooms Per Flush`, unloading after each, cancellable. A separate button because moving assets in the engine is slow. Delete from `Deprecated` by hand, or bring them back. |
| **Clear All Changes** | Erases the drawing. First detaches the levels, then clears the grid, the slicing and the bake state, then clears the manifest reference. A snapshot is taken automatically and `Restore` brings the drawing back — but not the levels; those come back from `Apply Changes`. The level and mesh assets are left on disk and are overwritten by the next build. |

### Edit

Buttons for editing a maze that has already been built. Disabled until it has been.

| Button | What it does |
|---|---|
| **Change Current Maze** | Detaches the levels, flattens the volume back to a plane and sets the active slice to the drawing plane. Flattening is exact: a column that had at least one solid cell stays a cell. |
| **Apply Changes To Current Maze** | The same work as `Apply Changes`. The two differ in what they mean, not in what they do: one says "build this", the other "put back what I was editing". |
| **Bind Decor To Rooms** | Moves actors you placed by hand from the persistent level into the level of the room they stand in, into the room's `DesignerObjects` Outliner folder with your own folders kept underneath. Actors moved earlier that now stand in another room move again; those in rooms a re-slicing abolished go to their new room, or back to the persistent level if they stand in none. Left where they are: the classes and the tag under `Build Settings → Decor`, info actors (game mode, world settings…), and actors implementing `IMazeKeepInPersistentLevel`. The log counts each case. |

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
| `Force Full Rebake` | Rebuild every room's meshes and rewrite every level, not only the changed ones. The build normally compares each room against a fingerprint of what it is made of. That comparison cannot see meshes or levels on disk being deleted or edited by hand — this is the way out when the two have drifted apart. **Not remembered between editor sessions**, and while it is on the build warns in the log. |
| **Clear All Room Merges** | Reset the whole room colouring (`Slicer → Merges`). One transaction: `Ctrl+Z` brings the colouring back. Needed before changing `Room Size XZ` or `Origin XZ`: merges are stored as lattice cell numbers and would shift. |
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
| `Generator` | created for you | optional | What fills the grid: manual, random, classic maze, from an image, from a mesh. Its fields: [15](#15-generators); also shown in the panel under `Maze Features`. |
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

### The Uniform Grid slicer

The `Slicer` field is an object with fields of its own; expand it with the arrow in Details.

| Field | Default | Touch? | Meaning |
|---|---|---|---|
| `Room Size XZ` | `(32, 32)` | optional, **before colouring** | Lattice step in cells: X along the level, Y is height. Sensible from 24×12 to 32×32; smaller if you are going to merge. |
| `Origin XZ` | `(0, 0)` | optional, **before colouring** | Lattice offset in cells. |
| `Merges` | empty | through the `Rooms` tool | Fused lattice cells: `Min Index` / `Max Index` pairs, inclusive. Not typed by hand — drawn with the `Rooms` tool. |
| `Discard Empty Rooms` | on | leave alone | A lattice cell without a single solid cell does not become a room. |

Rooms are named `R_XXX_ZZZ` after their lattice cell; a merged one after its bottom-left cell.

> **Copying the layout to a maze of the same geometry:** right-click the `Slicer` header → **Copy**, in the other asset → **Paste**. `Room Size XZ`, `Origin XZ` and every `Merges` entry come across. The condition: identical `Size XZ`, `Room Size XZ` and `Origin XZ`.

> **Build fingerprints.** The asset keeps two numbers per room that Details does not show: what its meshes were built from and what its level was written from. `Apply Changes` uses them to decide what to rebuild. The grid itself is stored on disk as one compressed block: a maze the size of Saboteur is about 100 KB. Older assets load as before and switch to the new format on their first save.

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
| `Outliner Decor Sub Folder` | `DesignerObjects` | Sub-folder for hand-placed decor moved in by `Bind Decor To Rooms`. Your own folders are kept beneath it: `Props/Barrels` becomes `…/DesignerObjects/Props/Barrels`. Empty — straight into the room folder. |
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
| `Merge Coplanar Faces` | on | Join neighbouring faces that lie in one plane and share a material into one quad: a wall twenty cells long is one quad instead of twenty — on a fine grid tens of times fewer triangles for the same picture. The UVs are world-space, so a texture runs across a joined quad exactly as across the cells. Off — one quad per cell face. |

### Decor

What `Bind Decor To Rooms` leaves in the persistent level even when it stands inside a room: the things a whole map shares rather than a room — the sun, the sky, the fog, where the player starts. Streamed out with a room, they would switch off with it.

| Field | Default | Meaning |
|---|---|---|
| `Decor Stay In Main Classes` | empty | Classes that stay; subclasses count. Info actors (game mode, world settings and the like) are never moved, listed or not. |
| `Decor Stay In Main Tag` | `MazeForge.StayInMain` | Put this tag on a single actor to keep it in the persistent level. |

### Rough

How the `Rough` brush chips the faces it was painted over. Every rough face is cut into a fine mesh and its points pushed into the rock — a little out of it — by a pattern of chips that depends only on the world position, so faces, bands and rooms meet without gaps.

| Field | Default | Meaning |
|---|---|---|
| `Rough Amplitude Cm` | `20` | How deep the chips go. Keep it under half a cell, or a wall one cell thick is cut through. |
| `Rough Chip Size Cm` | `40` | How big one chip is. |
| `Rough Subdivision Cm` | `12.5` | The step the faces are cut into. Smaller — sharper chips, many more triangles: a 50 cm face is 32 triangles at 12.5. |
| `Rough Overhang` | `0.25` | Share of the depth that juts out of the mass instead of into it. `0` — only into the rock. |
| `Rough Tilt` | `0.7` | How steeply each chip is tilted. `0` — flat steps; `1` — strongly slanted facets. |
| `Rough Seed` | `1` | Another pattern of chips. |
| `Rough Smooth Radius Cells` | `1.5` | Smooth the cube outline over this many cells before chipping: a staircase of cubes becomes a slope, a corner a rounded edge, and the chips go on that. `0` — off. |
| `Rough Smooth Max Cm` | `40` | How far smoothing may move a point off the cube. |
| `Rough Smooth Fill` | `0.2` | Share of that it may fill an inner corner by. `0` — outer corners are only cut into the rock and nothing grows into the passage, where the collision is empty. |
| `Rough Smooth XZ Only` | off | Smooth in the side view only: the edge between the face the camera looks at and the tunnel walls running into the depth stays sharp. |

Changing any of them rebuilds the rooms that have rough faces at the next `Apply Changes`.

### Palette

A map of "cell type → what to build it with". Filled in by the constructor, so it works out of the box.

Each entry has `Mesh` (empty — the stock cube), `Material Override`, `Editor Color` and a list of `Variants`.

> **Give `Floor` a material of its own.** `Mark Floor Surfaces` makes the ground `Floor` cells; without a `Material Override` or `Variants` on the `Floor` entry they build with the same look as the walls, and the work shows nowhere.

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
| `Lock Role` | `None` | required for doors and keys | `Door` closes the passage it stands in, floor to ceiling, until a key with the same `Lock Id` has been picked up; `Key` is picked up on touch. The reachability check and `Generate Locks` read it. In the game the actor needs a `Maze Lock` or `Maze Key` component. |
| `Lift Role` | `None` | required for lifts | `Platform` — a lift platform, placed with the `Lift` tool (two clicks); `Call` — a call terminal, placed with the brush. The platform's palette swatch switches on `Tool = Lift` by itself. In the game the actors need `Maze Lift` and `Maze Lift Call`. In detail: [18](#18-lifts). |
| `Rope` | off | required for ropes | The type is a rope over a gap, placed with the `Rope` tool (two clicks). `Rope Anchor Type` — the anchor at its ends. In the game the actor needs `Maze Rope`. In detail: [19](#19-ropes). |

> **Why `Scale` lives here.** The export owns every actor it makes: each `Apply Changes` destroys them and builds them again from the type. A size typed into the level survives exactly until the next `Apply`. Set here, it comes back every time.
>
> It scales the actor, **not the space it claims**: `FootprintCells` is declared, not measured. An object grown well past its footprint will happily overlap its neighbours. Grow the footprint with it when the object is meant to keep them away.

> **Careful with the `Free` anchor.** It fits anywhere empty and so effectively turns the check off. It also puts the object in the centre of the cell rather than on an edge, so the same crate ends up half a cell above its floor-standing siblings.

> **The `Floor` anchor and the `Floor` cell type are two different things with one name.** The anchor does not look at the cell type at all: it asks whether there is **mass** under the bottom edge of the footprint, and both `Solid` and `Floor` count as mass. A maze drawn entirely out of `Solid` is perfect ground for floor-anchored objects — the fact that the generator never paints `Floor` cells has no bearing on it.

> **The world border counts as support too.** It is painted into no cell and does not depend on depth — `IsBorderCell` looks only at X and Z — but the export builds it as real mass. So an object may legitimately stand on the bottom border of the map. It cannot be placed inside the border: those cells are impassable, and the brush says so in a line of its own.

### Ladder

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Top Piece Type` | empty | optional | The platform stood at the top of the ladder, its pivot on the level the ladder reaches. Give the platform type the `Free` anchor: there is a hole under it, not a floor. Empty — a ladder without a platform. |
| `Climb Height Property` | empty | optional | A number property on the actor, int or float, that gets the height of the climb — `NumberOfRungs`, say. Set at export, after which the construction script runs again, so a ladder that builds itself is exactly as tall as its climb. |
| `Climb Unit Cm` | `0` | with the property | Centimetres per one unit of that property. For a blueprint that stacks sections, the height of one section — not the rung spacing. The height is divided by it and rounded. `0` — the height goes in as centimetres. |
| `Climb Extra Units` | `0` | optional | Added after the division: minus the sections a top or bottom piece of the blueprint already has. |

### Variants

| Field | Default | Meaning |
|---|---|---|
| `Random Variant` | off | Wherever this type is named as the one to place for you — a generator's `Ladder Type`, the `Ladder` tool, `Fix Reachability`, `Generate Locks`, the start and exit markers — each placement may get any library type of the same `Category` and the same role (door, key, transition, lift, a ladder's platform or not). Random, but the same pick every time for the same spot. Give variants the same footprint: the space is checked for the type asked for. `Generate Objects` mixes on its own: a rule with `Target = Category`. |

> **Snapping measures what you see.** `Snap To Anchor Surface` takes the bounds of the actor's visible parts only: trigger and collision volumes (boxes, capsules, spheres) are left out, so a ladder's "you may climb" box below its first rung no longer lifts it off the floor.

---

## 5. The spawn asset (Maze Spawn Asset)

**Create:** Miscellaneous → Data Asset → **Maze Spawn Asset**.
**How many:** one per maze. Referenced from `Maze Grid → Build → Spawns`.

| Field | Default | Set it? | Meaning |
|---|---|---|---|
| `Library` | empty | **required** | Which library describes the types these placements refer to. |
| `Placements` | empty | output | The placements themselves. Filled by the brush and the generator. |
| `Next Id` | `1` | output | The next number to hand out. Only ever goes up. |
| `Ladder Map` | empty | output | The ladders and their platforms as last saved with `Save Ladder Map`. Only written by that button. See [17](#17-ladders). |

A separate asset from the grid because of lifetime, not tidiness. The maze is generated, cleared, restored and redrawn many times; the decor pass is later and far more expensive. The practical consequence: **`Clear All Changes` erases the drawing and leaves the objects alone**, and **the snapshot holds only the drawing**.

> **About the numbers.** `Next Id` is a counter, not the array length: deleting a placement does not renumber the others, and a number that has been used never comes back. The numbers key a runtime registry of what the player has done to each object, so a reused number is a save pointing at the wrong thing.
>
> A number is handed out at the **first export**, not when you draw. A placement the export refused to build gets none — otherwise "has an id" would stop meaning "exists in the world" the first time a wall was redrawn under a crate.

### One placement

| Field | Meaning |
|---|---|
| `Type Id` | Which library type. |
| `Cell XZ` | The bottom-left cell of the footprint. X along the level, Y up. |
| `Band` | The depth band. The object stands in the middle of it — in `Play`, exactly on the player's plane. |
| `Anchor` | The anchor it was placed by. The export checks it again against the geometry as it now stands. |
| `Rotation`, `Offset` | Hand adjustments, on top of the type's own. |
| `Id` | Identity for the save system. Handed out at the first export. |
| `Lock Id` | Doors and keys: which keys open which doors. Any name — `Red`, `Vault`. |
| `Climb Cells` | Ladders: how many cells the ladder climbs. Set by the ladder tools; the export sizes the ladder by it and does not check its anchor again. `0` — an ordinary object. |
| `Generated` | Put here by `Generate Objects`. Its next run replaces it. |
| `Structural` | Put here as part of the maze — ladders and the start and exit markers from the generator, ladders from `Fix Reachability` and `Place All Ladders`. `Generate Objects` keeps it; the next maze generation replaces it. |
| `From Lock Generator` | Put here by `Generate Locks`. Its next run, or a new maze, replaces it. |

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
| `Floor` | The ground: the horizontal surfaces the player can get onto. Geometrically the same as `Solid` — mass, collision — but with its own entry in the palette, so the ground gets its own material. Set by `Mark Floor Surfaces` (the generators, the brush after each stroke, the button), or painted by hand. |
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

### Lock role

| Value | Meaning |
|---|---|
| `None` | Neither. Most things. |
| `Door` | Closes the passage it stands in until a key with the same `Lock Id` has been picked up. |
| `Key` | Picked up on touch; opens every door with the same `Lock Id`. |

### Lift role

| Value | Meaning |
|---|---|
| `None` | Not a lift. |
| `Platform` | A lift platform, rides between its stops. |
| `Call` | A lift call terminal. |

### Edit tool

| Value | Meaning |
|---|---|
| `Cells` | Paints the mass of the maze. |
| `Objects` | Places objects from the library, one per click. |
| `Rooms` | Merges lattice cells into rooms. |
| `Path` | Plans the Classic generator's main path and branches. |
| `Ladder` | Stands ladders, two clicks each. |
| `Rough` | Marks rock to be chipped at bake. |
| `Lift` | Places a lift: the lower stop, then the upper. |
| `Rope` | Stretches a rope: one ledge, then the other. |

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
| `UWorld::DestroyActor: World has no context!` | **not your problem.** A false positive from the engine about attached sublevels. Appears only for levels the build rewrites |
| `Bake check: N hashes were stored for M rooms; A rooms had none, B had a different one.` | Why rooms are rebuilt. `had none` — no fingerprints yet (first build). `had a different one` for every room at once — something shared changed: `Build Settings`, the palette, the depth profile, the cell size |
| `Bake trace R_...: stored ...; now identity ..., geometry ..., settings ..., final ...` | The first mismatched room broken down by stage. Compare two such lines: the first number that differs names what changed |
| `Bake: Force Full Rebake is on, so every room was rebuilt` | The `Force Full Rebake` tick is on. Untick it after the run it was meant for |
| `Export: ... levels written N, unchanged and left alone M` | `M` levels did not change and were not touched. Normal for a repeated build |
| `Retire: rooms taken off the map N (...)` | After a re-slice, those rooms' levels were taken off the map and their folders removed. Files stay — `Move Stale Rooms To Deprecated` |
| `Retire: cleared what earlier builds left of rooms that no longer exist` | Levels and folders left behind by older builds were removed. One-off |
| `Deprecate: stale: levels N, meshes M; moved to Deprecated: ...` | The report of `Move Stale Rooms To Deprecated` |
| `... is stored in the old grid format ... Save it once to shrink the file.` | The maze asset was saved in the old format. Save it — the file shrinks by a factor of hundreds |

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

---

## 15. Generators

The maze asset's `Generator` field; the mode panel shows it under `Maze Features`. Every generator has the common fields below, then its own.

### Common fields

| Field | Default | Meaning |
|---|---|---|
| `Seed` | `1337` | The same value always gives the same maze. Also in the panel under `Generate`, with **Reroll**. |
| `Clear Before Generate` | on | Clear the grid first. Off — draw on top of what is already there. A cleared grid takes its generated ladders and generated locks with it; doors, keys and ladders placed by hand stay. |
| `Fill Back Wall After Generate` | off | Paint the back wall over the whole map once the drawing is done — the `Fill Back Wall` button, saved from being forgotten. |
| `Back Wall Variant` | `0` | The surface variant of that back wall. |
| `Random Surface Variants` | off | Mix the surface variants of the palette (Build Settings) over the generated mass, floor and back wall, instead of painting everything with the first one. Repeats with the same `Seed`. |
| `Surface Variant Patch Cells` | `8` | One variant per square patch of this many cells, the same through the whole depth. `1` — every cell on its own: noisy, and every seam between two materials is a face the bake cannot merge. |
| `Mark Floor Surfaces` | on | Once the drawing is done, make every horizontal surface the player can get onto `Floor` and the rest of the mass `Solid`, so the ground gets the `Floor` surface of the palette whatever the generator drew it as. Only reachable surfaces count: the air above has to be reachable the way `Check Reachability` sees it without gravity — a window sill, a pocket sealed in the mass or a slit he does not fit in stays `Solid`. Two edges count too: a roof open to the sky (with `Close Top` off), and the closed bottom border — it is stored nowhere, so where he walks on it, it gets `Floor` cells of its own. |
| `Floor Surface Cells` | `1` | How deep the `Floor` layer goes under each such surface. |
| `Agent Height Cells` | `2` | The player's height in cells. A passage lower than this is a wall to him, and the shaping keeps this much headroom above every floor. The stock capsule is 176 cm: two cells of 100, four of 50. |

### Dead ends

A passage end is a straight stretch of corridor closed by a wall with nothing leading on — no door, no hole, no ladder. It is found in the drawing itself, so it works the same for every generator, hand-drawn images included.

| Field | Default | Meaning |
|---|---|---|
| `Dead End Chance` | `0` | Share of passage ends that get a shape instead of a square wall. |
| `Dead End Shapes` | a mix of all | The shapes to pick from, by weight: `Flat` (left square — keeps a share of the ends plain), `Arrow` (closes in from above and below to a point), `Slant Down` (the ceiling slopes down to the floor), `Slant Up` (the floor rises to the ceiling in steps), `Round` (a half-circle), `Polygon` (a half-hexagon). Weight `2` comes up twice as often as `1`, `0` never. |
| `Dead End Length` | `0` | How far back from the end wall the shape reaches, in cells. `0` — at about 45 degrees. Always leaves at least one full-height column of the passage open. |
| `Dead End Max Height` | `0` | Passages taller than this are halls, and their ends are left alone. `0` — no limit. |

A passage lower than `Agent Height Cells` is not shaped either — the generator says so in the log.

### Erosion

Roughs up the outline: bites cells out of the mass and sticks cells back on to it. Afterwards stray single cubes and pinholes are cleaned up, so the outline is rough but has no debris.

| Field | Default | Meaning |
|---|---|---|
| `Erosion Amount` | `0` | Share of the outline changed per pass. `0` — ruler-straight. A wall or floor one cell thick cannot be bitten — that would go straight through — so only outcrops grow on it; give the mass two or three cells for real notches. |
| `Erosion Passes` | `2` | More passes let a bite deepen into a notch and a bump grow into an outcrop. |
| `Erode Floors` | off | Rough up the floors the player runs along. Off: a notched floor is a floor with steps in it, and the character catches on every one. |
| `Erode Ceilings` | on | The undersides of slabs. Nobody walks there — pure texture. |
| `Erode Walls` | on | The vertical faces. Pure texture. |
| `Erosion Keep Wall Cells` | `2` | Never bite into mass thinner than this: a wall eaten through is a passage nobody planned. |

Both shaping passes leave ladders, holes and the headroom where the player steps off alone.

### Classic

The textbook maze — every point reachable, by exactly one path unless loops are asked for — laid out for a side view: a lattice of corridor cells, walls between them, floor slabs between rows, and holes with ladders between floors. It starts with `Erosion Amount 0.3` and `Dead End Chance 0.5`. Unused lattice cells stay solid.

| Field | Default | Meaning |
|---|---|---|
| `Style` | `Long Corridors` | `Long Corridors` — long winding corridors, few deep dead ends. `Branchy` — many short branches and dead ends close to the main path. `Path First` — the main path from the start to the exit first, then a set number of branches a set depth each, then branches off those; can be planned by hand with the `Path` tool. |
| `Vertical Bias` | `0.35` | How likely an up-or-down link is next to a sideways one. Lower — longer horizontal runs and fewer ladders, which a side-scroller reads best. Never zero. |
| `Loop Chance` | `0` | Share of the remaining walls knocked through afterwards. `0` — a perfect maze; a little — shortcuts, so a wrong turn is not always a walk back. |
| `Corridor Width` | `6` | Length of one corridor cell along X, in grid cells. |
| `Corridor Height` | `4` | Clear height of a corridor. More than `Agent Height Cells` — the generator warns otherwise. |
| `Wall Thickness` | `1` | The walls between corridor cells. |
| `Floor Thickness` | `1` | The slabs between rows. |
| `Ladder Type` | empty | The library type stood in every hole: one ladder from the floor to the floor above, with its platform and `Climb Cells` — the same pieces the `Ladder` tool makes. Empty — no ladders: the holes can be fallen through and never climbed. |
| `Hole Width` | `2` | Width of the hole in a slab. Two at least: a one-cell hole catches the shoulders. |

`Path First` only. Lengths are in corridor cells of the lattice, not grid cells.

| Field | Default | Meaning |
|---|---|---|
| `Main Path Winding` | `0.5` | `0` — the main path goes the short way; `1` — it wanders as far as it can before the exit. |
| `Branches` | `6` | Branches off the main path, on top of any planned by hand. |
| `Branch Depth Min`, `Branch Depth Max` | `2`, `6` | How long each branch is. |
| `Sub Branch Chance` | `0.15` | Chance each cell of a branch starts a branch of its own, up to `Max Nesting` levels. |
| `Max Nesting` | `2` | `1` — branches only off the main path; `2` — off those too; and so on. |
| `Fill Leftover` | off | Join every unused corridor cell on as short spurs. Off — they stay solid, and the maze has exactly the branches asked for. |
| `Start Marker Type` | empty | A library type (an `Entry` point) stood at the start of the main path. |
| `Exit Marker Type` | empty | A library type (a `Gate`) stood at its end. |
| `Planned Main Path`, `Planned Branches` | empty | Drawn with the `Path` tool, in grid cells; cleared by `Clear Path Plan`. |

---

## 16. Reachability, paths, doors and keys

### Reachability

`Check Reachability` starts where the player arrives — the `Entry` points; with none, the lowest place to stand — and spreads everywhere he can be, `Agent Width Cm` wide and the generator's `Agent Height Cells` tall.

- Without `Require Ladders`: any space he fits in, in any direction. What it finds is the pieces nothing could lead into.
- With it: walking, falling, stepping up `Step Up Cm`, jumping `Jump Height Cm`, and climbing only where a `Climb` object stands — a ladder counts over the whole of its `Climb Cells`.
- Doors and keys are played out: a door seals its passage floor to ceiling until a key with its `Lock Id` can be reached — keys behind other doors included.

The result is drawn over the maze: rooms nobody can reach hatched red, rooms reached in part orange, rooms behind a door marked apart; doors and keys something is wrong with framed red. Before the maze is sliced into rooms, the bounds of the unreached pieces are drawn instead. Any change to the grid drops the overlay.

`Fix Reachability` joins every unreachable piece to the rest: a tunnel along the floor and, where the way leads up, a shaft `Fix Shaft Width Cells` wide, with a ladder of `Fix Ladder Type` when `Require Ladders` is on. Every door counts as open: a piece behind a door is a lock problem, not a missing passage, and carving round the door would quietly undo the design.

### Paths

`Analyze Paths` reads the maze as the player walks it: from the start (an `Entry` point, else the lowest place) to the exit (a `Gate` point, else the farthest place). `Path Status` gives:

| Number | Meaning |
|---|---|
| main path | Its length in cells. |
| branches, dead ends | Off the main path and in all; dead ends per nesting level. |
| junctions, loops | Where the main path forks; ways round. |
| branch depth | Average and greatest. |
| nesting | Levels of branch-off-a-branch. |
| confusion | 0–100: how often the way forks, how deep and nested the branches are, how many loops. |

Drawn over the maze: the main path yellow, branches by level, dead ends red, junctions, start and exit. `Find Seed` aims at the same numbers.

### Doors and keys

Two library types — `Lock Role = Door` and `Lock Role = Key`; the placement's `Lock Id` pairs them, and one red key opens every red door. Place them with the brush (`Paint Lock Id`) or with `Generate Locks`, which:

- puts each door where it closes off a piece of the maze, no smaller than `Min Locked Cells` and no larger than `Max Locked Share`;
- puts its key where it can be had without that door — off the main path when it can — `Min`…`Max Rooms Between Key And Door` rooms away, so a door and its key are never loaded around the player together;
- keeps to `Max Lock Nesting` and `Max Keys Held`; doors and keys placed by hand stay and are counted.

`Check Reachability` reports keys that cannot be had, doors that never open, and keys too close to their door.

### Gameplay components

What the game needs for doors, keys and used objects. Each reads its placement off the `Maze Object Id` component the export puts on the spawned actor, so one blueprint serves every colour.

| Class | Goes on | What it does |
|---|---|---|
| `Maze Keyring` | the player | Holds the keys: `Add Key`, `Has Key`, `Remove Key`; event `On Key Added`. |
| `Maze Key` | the key actor | `Lock Id` (empty — from the placement), `Pick Up On Touch` (on; off — call `Try Pick Up` yourself), `Destroy On Pick Up` (on); event `On Picked Up`. |
| `Maze Lock` | the door actor | `Lock Id` (empty — from the placement), `Unlock On Touch` (on; off — call `Try Unlock`), `Consume Key` (off — one key opens every door of its colour); events `On Unlocked`, `On Refused`. |
| `Maze Progress Subsystem` | the world | Remembers used placements by id: `Mark Used`, `Is Used`; `Used Placements` for a save game. A key taken or a door opened stays so when its room streams back in. |

---

## 17. Ladders

### What a ladder is

A ladder is a placement of a `Climb` type standing over a free column of cells, from one **level** to a higher one. A level is the floor under the column, or an **exit** above it — somewhere to step off right beside the ladder:

- going outwards along the row over open air, within about 2 m, the first cell with mass under it;
- at least 120 cm of that mass, with air under it and 150 cm of headroom above — a floor or a slab, not an erosion bump and not a step in the floor below;
- while the ladder's column there is in a gap with mass on both sides within 4 m — a hole or a shaft, not open air beside a pillar.

The thresholds are in centimetres, so they mean the same on a 10 cm grid and a 100 cm one.

A ladder put down by the tools is **one piece the whole length**: its height is stored on the placement as `Climb Cells`, and the platform of its type (`Top Piece Type`) stands on its top level. Ladders stack: the next one stands on the platform of the one below.

| Piece | Anchor | Pivot |
|---|---|---|
| a ladder on the floor | `Floor` | on the floor |
| a ladder on another ladder's platform | `Free` | on that level |
| the platform | `Free` | on the level the ladder reaches |

At export the height goes into the type's `Climb Height Property` (`Climb Cells` × cell height, over `Climb Unit Cm`, plus `Climb Extra Units`) and the construction script runs again; the log says `climbs N cm — NumberOfRungs = M`. The anchor of such a ladder is not checked again: it was fitted to its shaft. A ladder put down with the plain `Objects` brush has no `Climb Cells` and is measured at export from the floor under it to the first exit above.

### Putting them down

| How | What it does |
|---|---|
| the `Ladder` tool | Two clicks: the bottom, then the top — see the panel, [`Brush`](#brush). For designer mazes, where where a ladder goes is a decision. |
| **Place All Ladders** | A ladder in every hole that has none yet, from the floor to the first exit above, in the column nearest the middle of the hole. For generated mazes where every hole is meant to be climbed. Placed as part of the maze: the next generation replaces them. |
| the Classic generator | A ladder in every hole it cuts (`Ladder Type`) — the same pieces the tool makes. |
| **Fix Reachability** | A ladder in every shaft it cuts that has to be climbed (`Require Ladders`). |

The type: the `Climb` type picked in the palette, else `Fix Ladder Type`, else the Classic generator's `Ladder Type`. With `Random Variant` on the type, ladders mix with the other `Climb` types; a platform always belongs to the ladder it stands on.

### On the map

Ladders with `Climb Cells` are drawn the whole length in their type's `Editor Color`: a frame from the bottom level to the top with a rung every 25 cm. Platforms are a frame with a cross, like other objects. While the `Ladder` tool is between its clicks, the ladder to be is drawn in the style's `Brush Paint` colour and its platform row in `Brush Blocked`.

### The ladder map

In a designer maze every ladder is put down by hand, and a stray `Clear All Ladders` costs an afternoon. **Save Ladder Map** copies every ladder and platform into the spawn asset's `Ladder Map`; **Load Ladder Map** replaces the ladders now placed with it, keeping their ids so the save system still knows them. The map is written only by the button, so a bad state is never saved over a good one. Save the spawn asset afterwards.

### Out of the player's way

Objects stand in the middle of their band — in `Play`, on the player's plane — so a ladder stands right in his path. Give the ladder type and its platform type the same `Offset Y`: negative is towards the background (`-60`…`-80` with a 2-cell play band of 50 cm). The ladder's trigger boxes move with it: check they still reach the player's plane, or that climbing changes only X and Z of the character. The brush frames stay on the band's cell; `Offset` is not drawn.

---

## 18. Lifts

### Roles

| Where | Field | Meaning |
|---|---|---|
| Library type | `Lift Role` | `Platform` — the platform, `Call` — a call terminal. |
| Placement | `Climb Cells` | On a platform — its travel in cells, lower stop to upper. |
| Placement | `Lift Id` | The binding name. Empty: on a platform — `Lift_X_Z` from its cell, on a terminal — the nearest lift. |
| Placement | `Lift Starts At Top` | The platform waits at its upper stop. |
| Mode settings | `Paint Lift Id`, `Lift Starts At Top` | What lifts and terminals placed now get. |

### Binding terminals on export

With a `Lift Id` — to the lift of that name, its stop nearest in height. Without — to the nearest lift along X with a stop within 1.5 m of the terminal's level (in cells, rounded up, one at least). Found — a `calls lift … to its lower/upper stop` line in the log. Level format raised to 3: after updating the plugin the first `Apply Changes` rewrites every level once, so the terminals bind afresh. None found — a warning in the log, and the terminal answers `On Refused`.

### Maze Lift (on the platform)

| Field | Default | Meaning |
|---|---|---|
| `Lift Id`, `Stops`, `Start Stop` | from the placement | Name, stop heights above the spawn point (cm), starting stop. |
| `Speed` | 200 cm/s | Speed. |
| `Ignore Calls While Moving` | on | A call while riding is ignored. |
| `Ride When Boarded` | on | After a terminal's call (the lift came, or was already here) a player standing on the platform for `Board Delay` rides it to the other stop. No call — no ride; once there it waits for the next call. `Ready` (read-only) — called and waiting for a rider. |
| `Board Delay` | 0.75 s | How long to stand on it before it sets off. |
| `Crushes` | on | Riding down, it crushes a pawn under it. |
| `Crush Damage` | 10000 | Damage through `Apply Damage`. `0` — only `On Crushed`. |
| `Crush Reach` | 5 cm | How close under the platform counts as under it. |
| `Crush Inset` | 10 cm | Margin off the platform's edges, so someone beside it is not caught. |

Events: `On Departed(Stop)`, `On Arrived(Stop)`, `On Crushed(Victim)`. Functions: `Move To Stop`, `Move To Other Stop`. The stop the lift stands at is kept in `Maze Progress` and survives its room unloading.

### Maze Lift Call (on the terminal)

`Lift Id`, `Stop` — from the export. `Call On Touch` — call on touch. `Call` — call; `On Called` / `On Refused` — for an indicator.

### Maze Fall Damage (on the character)

| Field | Default | Meaning |
|---|---|---|
| `Safe Height` | 350 cm | A fall up to this is harmless. |
| `Lethal Height` | 1000 cm | From this height on — death. |
| `Min Damage` / `Max Damage` | 10 / 80 | Damage just over `Safe Height` / just under `Lethal Height`, linear between. |
| `Lethal Damage` | 10000 | Damage from `Lethal Height` on. |
| `Apply Damage` | on | Off — only `On Fall`. |

`On Fall(Height, Damage, bLethal)`; `Damage For Height` — what a fall from that height costs. Height runs from the fall's highest point (the top of the jump) to the landing.

### On the map

A platform is drawn in its type's color: a thin shaft frame from stop to stop, and a crossed frame where the platform waits. Between the `Lift` tool's clicks the shaft to be is `Brush Paint`, the lower stop `Brush Blocked`.

---

## 19. Ropes

### Fields

| Where | Field | Meaning |
|---|---|---|
| Library type | `Rope` | The type is a rope. Its palette swatch switches on `Tool = Rope`. `Allowed Anchors`, `Footprint Cells`, `Scale` and rotation do not apply. |
| Library type | `Rope Anchor Type` | The anchor type, stood on the ledge at each end. Empty — no anchors. |
| Placement | `Span Cells` | How many cells of gap the rope spans, from its own cell to the right. Its row is the one the player runs in; the cable lies along its bottom, level with the ledges' floor. |
| `Maze Object Id` | `Rope Length` | Length in cm, written by the export: `Span Cells × Cell Size X`. |

### How the span is found

The level comes from the first click: the floor under the cursor (within the ladder tolerance), or from inside mass the first free row above. Between the two clicks the row is checked: mass in the way — refused; cells with floor under them at both ends are trimmed off; nothing left — "no gap". An end without a ledge is allowed only against a wall. The span is tried at every level within the tolerance of both clicks' levels, and the best taken: ledges at both ends first, then nearest the first click. Anchors go on the ledges, with the `Floor` anchor. On export a rope belongs to the room of its cell, or, if that is in no room, to the room of its left ledge, its right ledge or any cell it crosses.

### Export

A rope is neither fit-checked nor snapped: the actor is spawned with no rotation or scale, its origin at the left end on the bottom of the row (+ the type's `Offset Z`), and runs along X. Anchors are ordinary objects.

### Maze Rope (on the rope)

| Field | Default | Meaning |
|---|---|---|
| `Length` | from the placement | Length in cm. |
| `Rope Mesh`, `Mesh Axis` | engine cylinder, `Z` | The rope piece and its long axis. Pieces are laid end to end along the spline, `round(Length / piece length)` of them (1…96), each pulled to an equal share — the texture keeps its scale. |
| `Rope Material` | empty | Instead of the mesh's materials. |
| `Thickness` | 3 cm | Thickness, whatever the mesh's own. |
| `Stretch Single Mesh` | off | One piece over the whole length. |
| `Rest Sag` | 4 cm | Sag with nobody on it, a parabola. |
| `Sag Under Feet` | 16 cm | The dip under the feet in mid-span; under a point load — two straight runs, less near the ends. |
| `Wave Speed`, `Damping` | 900 cm/s, 1.6 | The string: wave speed and damping. A node every ~20 cm, ends fixed. |
| `Step Kick`, `Step Distance`, `Board Kick`, `Kick Width` | 130 cm/s, 70 cm, 260 cm/s, 60 cm | Kicks: a step, stepping on / jumping off; a kick is a smooth bump `Kick Width` wide. |
| `Anchor Search Radius` | 250 cm | How far from each end to look for a `Maze Rope Anchor`. Found — a tie from the gap edge to the knot (the same mesh pieces), no physics. Not found — looked for once a second (the hook's room may load later). |
| `Stop Speed`, `Fall Delay` | 60 cm/s, 0.3 s | Slower than `Stop Speed` for longer than `Fall Delay` — drops through. `0` — never. |
| `Collision Depth` | 80 cm | Width of the carrying strip across the level (Y). |

The carrying strip is an invisible `BlockAll` box with its top on the cable's line; under load it drops with the cable under the feet. A character who fell ignores the strip until below it, off its ends, or 3 s later. Events: `On Boarded(Character)`, `On Fell(Character)`. `Get Sag At(X)` — sag in cm. With no load and no shiver the rope is not recomputed.

### Maze Rope Anchor (on the hook)

The point the rope comes out of: a component on the hook or post Blueprint, moved to its eye. It registers with the world when loaded; a rope takes the one nearest its end within `Anchor Search Radius`.
