# MazeForge — changes

## 0.8.0

Placed like a ladder, two clicks each, with their own tool, map drawing, reachability and docs:

- **Lifts** (`Tool = Lift`): a platform with two stops and call terminals bound by `Lift Id` or by level. Call, then step on — it rides. Crushes whoever is under it. `Maze Lift`, `Maze Lift Call`.
- **Ropes** (`Tool = Rope`): a taut cable over a gap, drawn as mesh pieces along a spline (the texture keeps its scale), simulated as a string — gives under the feet, shivers, settles. Stand still and you drop through. Ties to `Maze Rope Anchor` knots on the hooks. `Maze Rope`.
- **Sliding doors** (`Tool = Door`): a slab floor to ceiling that rides up as the player comes near and down when the zone is empty. With a key — opens once with it, takes it, and opens for anyone after. `Maze Door`.

Also:

- **Fall damage**: `Maze Fall Damage` on the character — safe, hurt, lethal by height from the top of the jump.
- **Ladders** (`Tool = Ladder`), **floor surfaces** (`Mark Floor Surfaces`) and the **Rough brush** (hewn rock) — since 0.5.0.
- Ladders, lifts, ropes and doors no longer mix in palette variants or ladder stacking.
- The palette picks the tool from the library at the moment of the click.
- A rope whose material lacks `Used with Spline Meshes` says so in the log, with where the tick is.
- Overlap bindings are unbound on EndPlay (rooms streamed out and back in no longer bind twice).
- Level format 3: the first `Apply Changes` after updating rewrites every level once.
