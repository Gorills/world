# Godot observer and development workflow

The observer runs the existing `SimulationState` in process through a Godot
GDExtension. It starts a pristine seed-42 world on day zero. Its camera is outside
the simulation: observing a region never refines water, allocates local history,
creates settlements or advances time by itself.

## Build and run

Install Godot 4.5 or newer (standard or .NET editor), CMake 3.20+, Ninja,
Python 3 and a C++20 toolchain. The bindings target the official Godot 4.5 API.

```bash
cmake --preset godot
cmake --build --preset godot
godot --path godot --editor
# Or launch the observer directly:
godot --headless --path godot --editor --import # One-time import if the editor has not been opened
godot --path godot
```

The first configure retrieves `godot-cpp` at commit
`e83fd0904c13356ed1d4c3d09f8bb9132bdc6b77` (official `godot-4.5-stable`),
checks its SHA-256, and builds only the bindings needed by the adapter.
No Git submodule initialization or .NET SDK is required.
Dependencies and generated bindings live under `build-godot/`; the extension is
copied into `godot/bin/`. Generated binaries and `.godot/` are ignored by Git.
Close the running game/editor before rebuilding the extension; native hot reload
is disabled to preserve the lifetime of the simulation owner.

Without Ninja, the equivalent platform-native build is:

```bash
cmake -S . -B build-godot -DWORLDSIM_BUILD_GODOT=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-godot --config Release --parallel 4
```

The descriptor includes Linux, Windows and macOS x86_64/arm64 library paths.
Build each binary on its target platform/toolchain. The default statically links
the simulation into the extension. With `BUILD_SHARED_LIBS=ON`, CMake also copies
the simulation library alongside it. Web/mobile exports are outside this first
desktop observer integration.

For an offline build, first obtain and extract the pinned bindings archive, then:

```bash
cmake --preset godot -DFETCHCONTENT_SOURCE_DIR_GODOT_CPP=/absolute/path/to/godot-cpp
cmake --build --preset godot
```

The override must contain the same pinned source revision. It bypasses the
download's hash check, so its provenance is the developer's responsibility.

The existing commands remain valid for work exclusively on the simulation:

```bash
cmake --preset core
cmake --build --preset core
ctest --preset core
```

## Real checkpoints

Godot and the CLI use the same compound `.wsc` format and deterministic day clock.
For example, create an evolved world and view it:

```bash
./build-godot/worldsim_cli simulation-new /tmp/century.wsc 42 36500
godot --path godot -- --checkpoint=/tmp/century.wsc
```

Use an absolute checkpoint path. The UI's save/load controls use an observer
checkpoint in Godot's `user://` application data directory. Loading is a staged
replacement: a failed load leaves the running simulation intact. Saving delegates
to the core's validated atomic checkpoint publication. A UI checkpoint save
replaces the previous observer checkpoint deliberately; loading a CLI checkpoint
does not overwrite its source through that quick-save control.

## Controls

| Input | Action |
|---|---|
| W/A/S/D | Fly forward, left, backward, right |
| Q/E | Descend/ascend |
| Hold right mouse button | Look around |
| Shift / mouse wheel | Boost / adjust flight speed |
| F or Home | Return to world overview |
| Space | Pause/resume simulation |
| Hover / left click | Inspect / pin a location |
| Fly to cell | Move the camera close to the selected cell |
| +1 day | Advance exactly one day and pause |
| +30 / +365 days | Advance a month/year in bounded daily batches and remain paused |
| Cancel / Space during queued advance | Cancel remaining days and remain paused |

The observer starts at a target of **16 days/second**. Select 1, 16, 64 or 365 d/s;
these are requested rates, bounded by hardware. A real second is not a physical
simulation day. Trees change over seasons and years, while weather/water change
daily. Use +365 days and watch the biomass and outlet-flow histories to see this.
Day zero has artificial initial water stores and empty channels: the first season
includes hydrologic spin-up, and terrain/drainage geometry stays fixed.

The sidebar selects Landscape, Plant biomass, Soil moisture, Temperature,
Elevation, River discharge, Surface water, Snow water or Precipitation and
optionally overlays the L0 grid. History plots retain up to 256 observed snapshots
with proportional model-day spacing and labelled automatic vertical ranges.
Fast-forward records the state at each rendered batch, so brief daily peaks can
fall between samples; use 1 d/s or single steps for daily detail. Plant change is
relative to the first observation. New/load resets this view-only history.
Its lower section contains global
totals, quick-save/load and new-world seed controls; scroll it on smaller windows.
Use `-- --paused` to start with time stopped, or `-- --seed=123` for a new seed.

## Boundaries and coordinates

`WorldSimBridge : RefCounted` owns one `SimulationState`. All native operations
run on the Godot main thread; the client requests complete days in a batch of at
most 32, stopping after an 8 ms stepping budget and rendering once per batch.
A single native day is indivisible and may exceed that budget. Fractional elapsed
days carry into the next frame; catch-up is capped at 32 days after a stall.
Rendering reads value snapshots only, with no C++ references retained across a
day advance or load. Full snapshots refresh after world changes, not every frame.
Large Europe-scale daily steps and checkpoint IO can pause the main thread;
background simulation requires a future explicit command/snapshot handoff.

This first observer accepts at most 4096 L0 cells (the default world has 256).
Larger checkpoints are rejected with a visible error before replacing the active
world or allocating per-cell Godot dictionaries. The CLI/core retain their
existing larger-world support. A bounded regional snapshot/streaming API is the
next step for observing Europe-scale worlds interactively.

Native positions remain double-precision world meters. The viewer subtracts the
world origin and maps horizontal kilometers to Godot X/Z; elevation maps to Y
with an explicit visual exaggeration. Terrain mesh resolution is bounded, and
sampling of the exclusive upper world edges stays inside the valid bounds.

The authoritative layers have different resolutions:

| Display | Source and meaning |
|---|---|
| Terrain | `World::sample_elevation`, static seeded elevation, not real geography |
| Weather | Current-day L0 temperature and precipitation (8192 m) |
| Ecology | Live L0 plant/animal carbon density, kg C/m² (8192 m) |
| Water | Authoritative coarse stores, or area-weighted refined stores for L0 overview |
| Point water | Exact L1 store where already refined, otherwise its L0 store |
| Rivers | Wet L0 drainage connections; schematic logarithmic width from completed-day discharge, with direction arrows |
| Local cover | Existing L2 vegetation/disturbance only, never created by inspection |
| Settlements | Existing persistent settlement positions and populations |

Vegetation instances are symbols of aggregate biomass. They are not persistent
individual trees; animal pools do not imply individual visible animals. A finer
terrain mesh does not increase the resolution of weather, hydrology or ecology.
The sea plane follows the configured datum. Surface-water depth is a cell-area
equivalent, snow is water equivalent, and channel volume is stored water, not a
water level. River Q is the mean release over the **last completed day**, including
for refined parents; displayed weather is the current day's forcing. Zero water
and zero discharge no longer render a river merely because static terrain has
river potential. Direction arrows do not encode velocity. The viewer does not
add flood or river hydraulics.

Water totals integrate the authoritative coarse/refined stores using actual
overlap areas at world edges. Outlet flow sums only terminal reaches (world exits
or discharge into ocean); summing all connected reaches would double-count water.
The inspector also shows groundwater, reach length and the model's heuristic
residence, which is not a measured travel time. See [the dynamics audit](AUDIT_DYNAMICS.md)
for measured evolution and the physical limits of this synthetic temperate world.

## Native API and errors

The bridge exposes `create_world`, `advance_day`, `save_world`, `load_world`,
`is_ready`, `get_last_error`, `get_terrain`, `get_frame` and `sample_point`.
Commands return `bool`; query failure returns an empty dictionary and records an
error. Invalid coordinates, out-of-bounds points and terrain resolutions outside
the supported range are rejected before sampling/allocation. C++ exceptions are
caught at the engine boundary. A new or loaded owner is published only after
successful construction.

`get_terrain` contains a row-major packed elevation array and bounds;
`get_frame` contains current-day L0 cells, global totals and settlements;
`sample_point` reports the owning cell/resolution plus existing local history.
Seeds are exposed as strings in snapshots so existing unsigned 64-bit checkpoint
seeds survive Godot's signed integer range. Interactive creation uses nonnegative
signed 64-bit seeds.

## Verification

```bash
ctest --preset godot
# Focus on the new boundary and scene during iteration:
ctest --test-dir build-godot -R 'observer|godot' --output-on-failure
```

CTest imports the actual Godot project, loads the extension, checks snapshots and
error paths, compares save/load continuation, checks non-mutating observation,
loads a C++-generated fixture with negative/partial bounds and refined water, and
launches the main scene. A Godot executable on PATH is required; alternatively
set `WORLDSIM_GODOT_EXECUTABLE` during configure. Missing Godot is reported during
configure and does not count as a successful engine check.

## Continuing development

- Add simulation rules, persistence and public APIs under `src/` and `include/worldsim/`.
- Translate core data/commands into Godot values under `adapters/godot/src/`.
- Add rendering, input and UI under `godot/`; keep simulation policy in C++.
- Cover native/engine boundary regressions in `tests/godot/` and core behavior in `tests/`.

See [the integration decision](GODOT_ARCHITECTURE.md) and the official
[GDExtension guide](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/gdextension_cpp_example.html)
and [godot-cpp CMake guide](https://docs.godotengine.org/en/stable/tutorials/scripting/cpp/build_system/cmake.html).
