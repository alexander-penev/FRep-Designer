# Example scenes

Small `.json` scene files demonstrating common patterns. Each one is a
valid input to `frep_multipath`, which loads a scene file and renders it
with the camera the file specifies. To render the whole set:

```bash
mkdir -p out
for f in examples/*.json; do
    name=$(basename "$f" .json)
    ./build/frep_multipath "$f" --width 800 --height 500 --out "out/$name.ppm"
done
```

(Earlier revisions of this file, of the top-level README and of the CI
workflow invoked a `frep_gpu_render` binary. No such target exists in
`CMakeLists.txt` — `frep_multipath` is the tool that takes a scene file.
`frep_render` and `frep_advanced` build their demo scenes in C++ and
accept no `--scene` argument; `frep_designer --scene FILE` opens one in
the GUI.)

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
then adds a back row of four `Translate → TwistY → RotateY → Box` columns. All
four are the same `Box(0.54, 2.45, 0.54)` at `ty = 0.75, tz = −5.32`, so their
bases sit exactly on the floor plane at `y = −1.7`. They differ only in twist:

| column | `tx` | `k` | phase (`a`) |
|---|---|---|---|
| 1 | −3.75 | +0.96 | 0 |
| 2 | −1.25 | +0.80 | 1.025 rad (58.7°) |
| 3 | +1.25 | +1.25 | 0.131 rad (7.5°) |
| 4 | +3.75 | −1.05 | 1.527 rad (87.5°) |

Columns 1 and 4 have nearly the same rate in opposite directions; 3 is the
fastest, 2 the slowest. The `RotateY` sits *under* the `TwistY`, so the total
rotation at height y is `k·y + a` — a pure phase offset that does not touch the
rate. Since a square cross-section repeats every 90°, the 87.5° on column 4 is
effectively zero: three of the four columns carry no real phase offset, and the
differing "starting rotation" you see comes from the differing rates and from
each column's own azimuth to the eye.

The `tx` values are snapped to a uniform 2.5 spacing — the same spacing as the
front row (box −2.5, sphere 0, cube +2.5), offset by 1.25. Fitting them freely
gave −3.82/−1.25/+1.26/+3.80, so the snap moves each by under 0.08 world units.

Column materials are ratios recovered from the figure (shading preserves them)
scaled to a 0.8 maximum; the absolute albedo magnitude is not recoverable from a
render.

## How the column numbers were obtained

`k` and `a` come from ray-probing: cast one ray per image row down a candidate
column's screen axis, record the rows where the surface normal jumps (a box edge
crossing the axis), and search `(k, a)` for the pair reproducing the rows
measured in the figure. Columns 1, 3 and 4 match their two measured crossing
rows to 1 px. Position, thickness and height come from the column silhouettes
plus the row where columns 2 and 3 meet the floor — both end at row 489, which
is what breaks the depth/size degeneracy, since a farther and proportionally
thicker column projects identically.

The probe exists because three simpler estimators each failed differently:
autocorrelating the shading pattern locked onto the second harmonic; a
closed-form phase formula was wrong because the crossing is observed in the view
frame while `RotateY` acts in the world frame; and a silhouette-width fit is
biased because a twisted box's silhouette is a ruled surface, not the per-height
cross-section that model assumed.

Column 2 is the weakest of the four and its rate rests on visual comparison, not
measurement: its two side faces are lit too similarly for the probe to resolve
its edge crossings (only one, at row 358, shows up at all, and only at the
loosest detection threshold). `k = 0.80` is where side-by-side comparison of
renders against the figure puts it — `0.55` reads as too few turns and `1.10` as
twice too many. The phase then follows from the one crossing that is visible.

## Camera

The file's camera is `position (0, 1.5, 8.82)`, `target (0, 0, 0)`, fov 55° — an
orbit camera, i.e. one the GUI viewport can express.

That is *not* the camera fitted to the reference figure, which lands at
`position (0, 1.13, 8.33)` looking at `(0, −0.30, 0)` with fov 56.3° (1.2 px rms
over the horizon row, the sphere's silhouette and the box's eight projected
corners). The tilt in that fit is an artefact: `fig1_scene_only.png` is a
viewport screen capture, and re-fitting with the target pinned to the origin plus
a vertical crop offset reaches the same 1.1 px rms — the crop moves the optical
centre, and a free fit absorbs that as a tilt. So the figure's exact framing at
807×903 is not reproducible from an orbit camera; the column *geometry* above is
unaffected, since it was solved under the fitted camera and lives in world space.

**Loading this file in the GUI will not restore its camera.**
`Viewport::recompute_camera()` (gui/viewport.cpp) unconditionally overwrites
`scene_->camera()` from the viewport's own orbit state, which starts at the
hardcoded `cam_yaw_ = 0, cam_pitch_ = 0.3, cam_dist_ = 9` — the file's camera
never reaches it. The real-time Vulkan viewport does seed its orbit from the
scene (`orbit_init_`); the classic viewport does not. Render headless to get the
camera the file specifies:

```bash
./build/frep_multipath examples/07_default_twisted_columns.json --width 807 --height 903 --out out/fig1.ppm
```

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

