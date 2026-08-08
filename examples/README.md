# Example scenes

Small `.json` scene files demonstrating common patterns. Each one
is a valid input to `frep_gpu_render --scene`. To render the whole
set:

```bash
mkdir -p out
for f in examples/*.json; do
    name=$(basename "$f" .json)
    ./build/frep_gpu_render "out/$name.ppm" 800 500 --scene "$f"
done
```

| File | Demonstrates |
|---|---|
| `01_csg_basic.json` | Three CSG ops (Difference, SmoothUnion, Intersection) side by side |
| `02_twisted_column.json` | TwistY deformation on a thin box |
| `03_smooth_union_blob.json` | Two spheres blended via SmoothUnion |
| `04_patterned_spheres.json` | Solid, Checker, and Stripes pattern materials |
| `05_carved_sphere.json` | Sphere with a cubic Difference cut-out |
| `06_textured_objects.json` | Wood-textured sphere + marble-textured cube (loaded from `textures/`) |
| `07_default_twisted_columns.json` | The GUI's default startup scene plus a back row of four twisted columns, one per twist rate |

`07_default_twisted_columns.json` is the figure scene (`docs/gallery/fig1_scene_only.png`).
It reproduces `build_default_scene()` from `gui/main.cpp` node for node — red
sphere, blue box, green rounded cube, yellow SmoothUnion blob, grey floor plane —
then adds a back row of four `Translate → TwistY → RotateY → Box` columns, all the
same size, differing only in twist rate, direction and starting phase:

| column | `tx` | `k` | phase (`a`) | match to the figure |
|---|---|---|---|---|
| 1 | −3.75 | +0.96 | 0° | 1 px |
| 2 | −1.25 | +0.54 | 52.5° (0.916 rad) | 0 px (one crossing) |
| 3 | +1.25 | +1.25 | 7.5° (0.131 rad) | 1 px |
| 4 | +3.75 | −1.05 | 87.5° (1.527 rad) | 1 px |

The `tx` values came out of the fit as −3.82/−1.25/+1.26/+3.80 and are snapped
to a uniform 2.5 spacing — the same spacing as the front row (box −2.5, sphere 0,
cube +2.5), offset by 1.25. The snap moves each column by under 0.08 world units,
inside the fit's own residual.

Columns 1 and 4 have the same rate in opposite directions; 3 is the fastest.
The `RotateY` sits *under* the `TwistY`, so the total rotation at height y is
`k·y + a` — a pure phase offset that does not touch the rate.

Note that phases 87.5° and 0° are the same thing — the pattern of a square
cross-section repeats every 90° — so three of the four columns sit at
effectively zero phase. The apparent "different starting rotation" between
columns comes from their differing rates and from each column's own azimuth
relative to the eye, not from a modelled offset.

`k` and `a` were found by ray-probing: cast one ray per image row down a
candidate column's screen axis, record the rows where the surface normal jumps
(a box edge crossing the axis), and search `(k, a)` for the pair reproducing the
rows measured in the figure. This bypasses every closed-form silhouette model —
and it had to, because three simpler estimators each failed in a different way:
autocorrelation of the shading pattern locked onto the second harmonic; a
closed-form phase formula was wrong because the crossing is observed in the view
frame while `RotateY` acts in the world frame; and a silhouette-width fit is
biased because a twisted box's silhouette is a ruled surface, not the per-height
cross-section the model assumed. Column 2's rate is pinned by a count rather
than a position: over rows 168–486 the figure shows exactly **one** crossing,
which `k = 1.10` turns into 3–4 and `k = 0.80` into 2–3.
All four are `Box(0.54, 2.45, 0.54)` at `ty = 0.75, tz = −5.32`, so their bases
sit exactly on the floor plane at `y = −1.7`.

The camera — `(0, 1.13, 8.33)` looking at `(0, −0.30, 0)`, fov 56.3° — and the
column geometry were solved back from the reference image rather than eyeballed:
the horizon row, the sphere's silhouette and the box's eight projected corners
fix the camera to 1.2 px rms; the column silhouettes plus the row where columns
2 and 3 meet the floor fix their position, thickness and height to 2.3 px rms.
The twist rates come from autocorrelating each column's shading pattern along
its own axis. Phases are the least certain number in the table.

The `textures/` subdirectory contains the BMP texture assets referenced
by the textured scenes — these are procedurally generated (256×256
each), bundled so the examples render out-of-the-box without
external downloads.

These files use only built-in node types — no plugin registry needed
to load them. They're also good starting points for hand-editing: open
any `.json` in a text editor to see the schema documented in
`docs/USER_GUIDE.md`.

To regenerate this set from C++:

```bash
# See the build process in tools/build_examples.cpp (not committed —
# scene files are committed instead so they're language-independent).
```

The scenes are also exercised programmatically by `frep_gallery`
(`tools/gallery.cpp`), which renders richer variants with multiple
lights and SSAA.

## Benchmark scenes (`benchmark_*.json`)

A set of ready-made scenes for manual performance testing, so you don't
have to hand-build complex scenes to compare render modes or check the
adaptive spatial-guard behaviour. Two families:

**Simple** — cheap per-object SDFs (~2 nodes each). The spatial-guard
heuristic leaves these on the inlined path (guarding bare primitives
doesn't help; the vectorised `min()` wins):

- `benchmark_simple_spheres_27.json` — 27 spheres
- `benchmark_simple_boxes_64.json` — 64 boxes
- `benchmark_simple_spheres_125.json` — 125 spheres (count stress)

**Heavy** — expensive per-object SDFs (twist + smooth-union, or CSG;
~4–6 nodes each). Once the host is calibrated, the heuristic switches
these to the guarded path, where measured speedups are ~3–6.5×:

- `benchmark_heavy_twist_27.json` — twisted box + smooth-union sphere
- `benchmark_heavy_twist_64.json`
- `benchmark_heavy_twist_125.json` — large; inline baseline is slow
- `benchmark_heavy_csg_48.json` — box-minus-sphere difference per object
- `benchmark_mixed_48.json` — half cheap / half heavy (tests averaging)

Load any in the app (File → Open) and toggle **Render → Adaptive spatial
guards** to compare, or render headless. The first heavy scene compiled
triggers a one-time guard calibration (~2 s); simple scenes never do.

To regenerate the benchmark set:

```bash
# builds the scenes through the SceneGraph API → schema-valid JSON
clang++ -std=c++23 tools/gen_benchmark_scenes.cpp build/libfrep_core.a ... -o gen
./gen examples
```

