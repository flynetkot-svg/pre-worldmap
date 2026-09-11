# MazeForge

Fast prototyping of 2.5D mazes for a side-scrolling game.
Pipeline: **draw it → slice it into rooms → play, rooms stream themselves**.

Full documentation lives in `Content/Game/TechDoc/`:

- `MazeForge_UserGuide_EN.md` — how to use the plugin, from zero
- `MazeForge_TZ_AsBuilt_EN.md` — architecture and the reasoning behind each decision

## Installation

1. Copy `MazeForge` into `<Project>/Plugins/`. Plugins in the project folder are
   enabled automatically.
2. Right-click the `.uproject` → **Generate Visual Studio project files**.
3. Build the **Development Editor** configuration.

The plugin contains C++ and has to be compiled. Copying it into a blueprint-only
project will not work.

### Required engine settings

Add this to `Config/DefaultEngine.ini`:

```ini
[SystemSettings]
s.ForceGCAfterLevelStreamedOut=0
s.ContinuouslyIncrementalGCWhileLevelsPendingPurge=0
gc.TimeBetweenPurgingPendingKillObjects=120
s.AdaptiveAddToWorld.Enabled=1
```

The first two lines are not optional. By default the engine runs a **full blocking
garbage collection every time a level streams out**, and the character visibly
stutters on every room unload. That is engine behaviour, not a plugin bug.

## World axes

```
X — along the level      Z — height       Y — depth (camera axis)
```

Depth is divided into three bands. The player is locked to the XZ plane at the
exact centre of Play:

```
 +Y  FOREGROUND   decoration in front of the player, no collision   (closer to camera)
     PLAY         maze geometry and all collision                   <- player is here
  0  BACKGROUND   decoration behind the player, no collision
```

World zero on Y sits at the centre of Play, so the stock
`SetPlaneConstraintNormal(0, 1, 0)` works without a single change in game code.

## Viewport: Left or Right, never Front

Drawing happens in the XZ plane. The view Unreal calls **Front** looks along X and
shows YZ — the cursor ray runs parallel to the drawing plane and the brush has
nothing to hit.

Use an orthographic **Left** or **Right** view (looks along Y, X horizontal,
Z vertical) or ordinary perspective. If the view is wrong, the status line in the
viewport says so in plain text.

## Two rules that must not be broken

**1. `MainLevel` stays an ordinary level.** Do not convert it to World Partition:
sublevels and `ULevelStreamingDynamic` do not coexist with WP, and the whole room
streaming system breaks.

**2. Do not remove the `MazeForge.Generated` tag.** Re-exporting a maze deletes only
tagged actors from a room level. Everything a designer placed there survives the
re-export precisely because of that tag.

## Modules

| Module | Type | Contents |
|---|---|---|
| `MazeForgeCore` | Runtime | Grid, types, assets, generators, slicers |
| `MazeForgeStreaming` | Runtime | Streaming pool, rules, observer component |
| `MazeForgeEditor` | Editor | Drawing mode, preview, mesh bake, level export |

Dependency rule: **Editor → Core**, **Editor → Streaming**, **Streaming → Core**.
There is not a single reverse dependency, so the voxel grid, the slicers and the
whole editor pipeline never reach a shipping build. At runtime the game sees only
the manifest.

## Extension points

A new implementation is one subclass. It shows up in the dropdown in Details on its
own, and its `UPROPERTY` fields appear in the parameter panel, without a single line
of UI code.

| What you are adding | Base class |
|---|---|
| A way to fill the grid | `UMazeGeneratorBase` |
| A way to slice into rooms | `UMazeRoomSlicerBase` |
| A load priority rule | `UMazeStreamingRule` |
| A way to build room geometry | `IMazeMeshBuilder` |
