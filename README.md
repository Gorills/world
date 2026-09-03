# WorldSim v0.11.0 — persistent channel transport

Headless C++20 simulation core for a large persistent world. v0.11 extends the unified v0.10 lifecycle with conserved in-channel water and finite daily travel time while keeping channel state inside the existing multiresolution-water authority and compound checkpoint.

## Implemented

- Engine-independent C++20 core (`worldsim`).
- C ABI suitable for thin Unity/Godot/Unreal adapters.
- Spatial hierarchy:
  - L0 climate / weather / continental drainage / coarse dynamic water / persistent channel storage / parent-equivalent soil: 8192 m;
  - L1 regional terrain / authoritative refined drainage / selective detailed dynamic water / spatial soil heterogeneity: 1024 m;
  - L2 local persistent history: 64 m, 16×16 per L1 cell;
  - future entities use continuous coordinates.
- Europe-scale world bounds without eager L1/L2 allocation.
- Deterministic procedural terrain, climate and soil scaffolding.
- Configurable sea-level datum.
- Whole-world authoritative L0 drainage and fixed 8×8 authoritative L1 refinement.
- Capacity-aware dynamic water with conservative coarse/fine terrestrial ownership.
- Persistent conserved L0 channel storage with a fixed one-day e-folding linear-reservoir release.
- One-L0-edge-per-day channel causality: current-day runoff/arrivals cannot be re-released during the same global day.
- Refined-parent channel ingress through the authoritative L1 drainage graph without bypassing the L0 travel-time boundary.
- Explicit whole-world transient `WeatherState` with coherent daily precipitation, temperature and PET forcing.
- `SimulationState` as the application-level owner of:
  - `World` persistent L2 history;
  - derived continental topology;
  - `WeatherState`;
  - `MultiresolutionWaterState`, including channel storage.
- One exact simulation day shared by weather, coarse water and every refined water tile.
- Runtime refinement, aggregation, daily advance and persistent surface mutation through the unified owner.
- Compound versioned checkpoints containing World + Weather + Multiresolution Water as one validated generation.
- Multiresolution-water persistence v3 with explicit v2 → zero-channel migration.
- Checkpoint section lengths/checksums, strict corruption/truncation checks and component identity/clock validation.
- Same-directory temporary checkpoint publication with validated atomic replacement of an existing target.
- Migration from existing `World::save()` files while preserving materialized L2 history.
- Additive channel queries on standalone multiresolution-water and unified simulation C handles without changing existing C POD layouts.
- CLI `simulation-run` and `simulation-resume` paths.
- GCC/Clang warnings-as-errors, ASan/UBSan, and complete MSVC shared-library consumer tests.
- Europe-scale water, weather+water and unified checkpoint benchmarks in GCC CI.

## Core ownership invariants

### Simulation lifecycle

```text
SimulationState
├── World                      authoritative persistent L2 history
├── Continental topology       derived from World
├── WeatherState               authoritative transient atmosphere
└── MultiresolutionWaterState  authoritative conserved water
    ├── terrestrial L0/L1 stores
    └── persistent L0 channel storage
```

Component views exposed by `SimulationState` are const. Application-level state changes go through simulation commands.

### Time

```text
simulation.day
== weather.day
== multiresolution_water.day
== every refined water tile day
```

Daily weather is prepared first, authoritative water advances atomically, and only then is weather committed. A rejected water step cannot split clocks.

### Terrestrial water

```text
unrefined parent
    L0 terrestrial stores = authoritative
    L1 tile                = absent

refined parent
    L0 terrestrial stores = zero
    L1 tile                = authoritative
```

The same terrestrial water volume is never independently advanced at both resolutions.

### Channel water

Channel storage remains one L0-owned conserved volume per terrestrial parent regardless of whether that parent's terrestrial stores are coarse or refined.

For each day:

```text
release = channel_storage_at_day_start × 0.6321205588285577
```

Only start-of-day storage releases. Current-day runoff, upstream arrivals and refined-tile outlet volume remain in channel storage until a later day, so one parcel crosses at most one L0 edge per global day.

### Atmosphere

Static `ClimateSample` remains the reproducible long-run baseline. `WeatherState` owns transient atmospheric anomaly state. Hydrology consumes precipitation, temperature and PET forcing records rather than embedding atmosphere.

## Conservation

For each global mixed-resolution water day:

```text
storage_before + terrestrial_precipitation
≈ storage_after + terrestrial_ET + terminal_outflow
```

`storage_before` and `storage_after` include terrestrial coarse/refined stores plus all persistent L0 channel storage.

The v0.11 Europe fixture observes a maximum relative daily balance residual of `5.886e-9` on the audited GCC Release run.

## Compound persistence

`SimulationState::save_checkpoint()` publishes one generation with exactly three authoritative sections:

1. World;
2. Weather;
3. Multiresolution Water.

Channel storage is part of the Multiresolution Water section; it is not a fourth persistence authority. Continental topology is derived from World and is rebuilt on load rather than serialized.

Multiresolution-water format v3 adds one `double` channel volume per L0 cell. A valid v2 file loads with zero channel storage because v2 had no persistent in-channel state. Format v1 remains rejected because it predates the current spatial soil-capacity semantics.

The compound save path serializes component sections privately, records byte lengths and FNV-1a corruption checksums, assembles a same-directory publish file, validates the completed container, flushes it, then atomically replaces the target. Failures before publication leave an existing target untouched.

The loader validates container structure/checksums, loads World, rebuilds topology, loads weather/water against that identity, then requires all component clocks to equal the checkpoint global day before exposing state.

FNV-1a is accidental-corruption detection, not cryptographic authentication. Current binary persistence uses native POD representations; cross-endian save-file portability is not guaranteed. POSIX publication is atomic under normal filesystem rename semantics, but full power-loss durability of the directory entry is not claimed because the parent directory is not explicitly fsynced after rename.

See `docs/SIMULATION.md` for the v0.10 lifecycle/container contract and `docs/CHANNEL_TRANSPORT.md` for the v0.11 channel-state contract.

## C ABI

Existing C POD layouts and existing function signatures are unchanged in v0.11.

Additive read-only queries expose one L0 channel volume or total channel storage on:

- `ws_multiresolution_water_state`;
- `ws_simulation_state`.

Channel mutation remains solver-owned; the ABI does not expose arbitrary setters.

## Legacy World migration

Existing `World::save()` files remain readable and their format is unchanged.

A pre-v0.10 World can become a day-zero unified simulation without losing materialized L2 history:

```cpp
auto world = worldsim::World::load("legacy.ws");
auto simulation = worldsim::SimulationState::from_world(std::move(world));
```

Weather, terrestrial dynamic water and channel storage start from their deterministic day-zero state because those transient authorities were not stored inside the legacy World file.

## Scientific/model limitations

- Terrain/climate/soil fields remain synthetic scaffolding, not reconstructed Europe.
- Weather is a coherent stochastic L0 layer, not numerical weather prediction.
- No L1 atmospheric downscaling, pressure, wind, humidity, radiation or explicit cloud physics.
- PET remains a simple temperature proxy.
- Soil remains one vertically aggregated bucket with no lateral groundwater aquifer state.
- Channel travel time is conserved and persistent, but uses one fixed global linear-reservoir coefficient rather than reach-specific geometry/velocity.
- No channel capacity, flood-wave/backwater hydraulics, wetlands or floodplains.
- No erosion, sediment or vegetation feedback.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Shared library:

```bash
cmake -S . -B build-shared -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build-shared
```

Europe-scale benchmarks:

```bash
./build/worldsim_multiresolution_water_benchmark
./build/worldsim_weather_benchmark
./build/worldsim_simulation_benchmark
```

One GCC Release v0.11 checkpoint observation on the 449,208-L0 / 64-refined fixture measured approximately:

- simulation construction: `815.272 ms`;
- five unified days: `776.130 ms`;
- checkpoint save: `170.951 ms`;
- checkpoint load including topology reconstruction: `939.440 ms`;
- checkpoint size: `21,769,048 bytes` (`~20.76 MiB`);
- persistent channel storage after five warmup days: `85,772,959,568.875 m³`;
- peak benchmark RSS: `238,800 KiB`;
- maximum relative water-balance residual: `5.886e-9`.

The benchmark requires exact channel equality across every L0 cell after checkpoint reload and again after one deterministic future day. Timings/RSS are environment-specific observations, not API guarantees.

## CLI

Create a legacy World save:

```bash
./build/worldsim_cli demo demo.ws
```

Migrate that World into a compound simulation, advance 30 days and checkpoint:

```bash
./build/worldsim_cli simulation-run demo.ws campaign.wsc 30
```

Resume the exact compound generation for another 30 days and atomically replace the checkpoint:

```bash
./build/worldsim_cli simulation-resume campaign.wsc 30
```

A zero-day `simulation-run` is valid for migration-only checkpoint creation.

Legacy focused solver paths remain available:

```bash
./build/worldsim_cli continent demo.ws 25
./build/worldsim_cli continental-water demo.ws 365
./build/worldsim_cli weather-water demo.ws 365
./build/worldsim_cli watercycle demo.ws 3 4 365
```

## Documentation

- `docs/ARCHITECTURE.md` — current ownership and scheduling decisions.
- `docs/SIMULATION.md` — unified lifecycle/checkpoint/C ABI/CLI contract introduced in v0.10 and extended by v0.11 water persistence.
- `docs/CHANNEL_TRANSPORT.md` — v0.11 persistent channel ownership, routing and persistence contract.
- `docs/WEATHER.md` — transient weather model.
- `docs/MULTIRESOLUTION_WATER.md` — historical v0.8 conservative coarse/fine terrestrial ownership design with current-status pointers.
- `docs/SOIL.md` — static spatial soil properties and capacity integration.

## Audits

- `docs/AUDIT_v0.1.md` through `docs/AUDIT_v0.8.md` — earlier spatial/hydrology/soil/weather milestones.
- `docs/AUDIT_v0.9.md` — selects unified simulation/checkpoint ownership.
- `docs/AUDIT_v0.10.md` — validates the unified lifecycle and selects persistent channel transport.
- `docs/AUDIT_v0.11.md` — validates conserved channel travel time and the v3 persistence/ABI/checkpoint integration.

## Next bounded milestone

The new ownership boundary no longer needs redesign. The strongest remaining channel simplification is the single global residence-time coefficient. A later bounded slice can derive deterministic per-reach transport time from authoritative reach/topology/world fields while preserving the same conserved channel array, start-of-day release causality and checkpoint authority.
