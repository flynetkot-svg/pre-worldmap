# Maze path structure — analysis and control

Date: 2026-10-23 · Plugin: MazeForge (UE 5.8) · Status: draft for approval

## Goal

A maze must read as a puzzle, with or without doors: one **main path** from the start to the exit,
**dead-end branches** off it, **sub-branches** off those, in numbers and depths the designer sets, so
the player gets lost, backtracks, and doors and keys add variety on top. Erosion must not leave
single loose cubes or one-cell holes.

## Definitions

- **Start** — Entry transition points (several = several starts); none → the lowest place to stand
  (as Check Reachability does today).
- **Exit** — a Gate transition point; several → the farthest by path from the start; none → the
  farthest reachable place to stand, reported with a suggestion to put the exit there.
- **Path graph** — built from the reachability solver's positions (same agent size, gravity mode,
  door/key progression). Positions are compressed into nodes: **junctions** (3+ ways on), **dead
  ends** (1 way), **start/exit**; edges are the corridors between them, with length in cells.
- **Main path** — the shortest walk start → exit on that graph, doors passable in progression order.
- **Branch** — a connected piece of the graph hanging off the main path at one junction.
  **Depth** = longest walk from its attach point to its farthest dead end. **Nesting** = how many
  junctions deep its sub-branches go (branch off a branch = level 2, …).
- **Loop** — an edge that closes a cycle (two ways between the same places). Counted separately.

## Analysis (any generator)

Button **Analyze Paths** (Reachability section), result in the status line, the log and the overlay:

- Main path: length (cells and rooms), turns, vertical changes.
- Branches: count; per branch depth and nesting; dead ends total; dead ends per nesting level;
  junctions on the main path; loops.
- **Confusion score** 0–100: weighted from branch count per main-path length, average branch depth,
  share of dead ends deeper than N, junction density — one number to compare seeds.
- Overlay: main path as a bright line, branches coloured by nesting level, dead ends as dots,
  junctions as small circles, start/exit markers.

## Control

### Classic generator — path-first mode

New Style **Path First**: carve the main path start → exit on the lattice (length/winding targets),
then grow branches from main-path cells, then sub-branches from branches, then fill what is left as
short spurs so every cell is used. Settings:

| Setting | Meaning |
| --- | --- |
| Main Path Share | share of lattice cells on the main path (0.2–0.5) |
| Main Path Winding | 0 straight … 1 tortuous |
| Branches | count off the main path |
| Branch Depth Min/Max | in lattice cells |
| Sub-branch Chance | chance a branch spawns a branch of its own, up to Max Nesting |
| Max Nesting | levels of branch-off-branch |
| Loops | as today (Loop Chance), applied after |

Existing styles (Long Corridors, Branchy) stay as they are.

### Any generator — seed search

**Find Seed** in Maze Features: tries N seeds (default 20), analyses each, keeps the one whose
metrics are closest to the **Path Targets** (target dead ends, min branch depth, max nesting, min
confusion score). Same targets drive both modes.

## Door and key limits (extends Locks)

Checked by Check Reachability, respected by Generate Locks:

- **Max Lock Nesting** — doors in a row a key can hide behind (key behind door behind door…).
- **Max Keys Held** — most keys the player carries at once in the intended play-through.
- **Max Rooms Between Key And Door** (upper bound next to the existing minimum).
- **Max Locked Share** — the most of the maze one door may close off.
- Generate Locks prefers doors on the main path or at branch entrances, keys in branches — the
  puzzle follows the path structure.

## Erosion cleanup

After erosion, in every generator: a solid cell with no solid side neighbour is removed (no loose
cubes); an empty cell enclosed on all four sides (or narrower than the agent and closed) is filled
(no one-cell holes). Protected cells (ladder shafts) untouched.

## Out of scope

Smooth slope geometry; save-game persistence of keys; enemies/items placement by path.

## Order of work

1. Path graph + Analyze Paths + overlay.
2. Erosion cleanup (small, independent).
3. Classic Path First.
4. Seed search with Path Targets.
5. Lock limits + path-aware Generate Locks.
