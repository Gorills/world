# WorldSim v0.12.0 — persistent reach-aware channel transport

Headless C++20 simulation core for a large persistent world. v0.12 closes the uniform-channel-residence limitation with bounded per-reach transport derived from D8 length, filled-elevation slope and accumulated discharge while preserving the existing multiresolution-water authority and compound checkpoint.

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
- Persistent conserved L0 channel storage with bounded per-reach residence derived from D8 length, filled-elevation slope and accumulated discharge.
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
- Multiresolution-water persistence v5 with explicit v2/v3/v4 migration and no duplicated persisted transport metadata.
- Checkpoint section lengths/checksums, strict corruption/truncation checks and component identity/clock validation.
- Same-directory temporary checkpoint publication with validated atomic replacement of an existing target.
- Migration from existing `World::save()` files while preserving materialized L2 history.
- Additive channel-storage and derived transport queries on standalone multiresolution-water and unified simulation C handles without changing existing pre-existing POD layouts.
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

For each terrestrial L0 reach, the derived residence heuristic is:

```text
length_cells = 1 for cardinal D8, sqrt(2) for diagonal D8
slope        = max(downhill_gradient, 1e-5)
discharge    = max(accumulated_discharge_m3_s, 1)

residence_days = clamp(
    length_cells
    × (slope / 1e-5)^-0.08
    × (discharge / 100)^-0.06,
    0.75,
    3.0)

release_fraction = 1 - exp(-1 / residence_days)
release          = channel_storage_at_day_start × release_fraction
```

Length remains the dominant factor. Slope and accumulated discharge only weakly modify residence, and the hard bounds prevent a low-resolution heuristic from pretending to resolve real sub-daily river hydraulics. A flat cardinal reach at the 100 m³/s reference discharge retains the legacy one-day residence.

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

The v0.12 Europe fixture observes a maximum relative daily balance residual of `5.886e-9` on the audited GCC Release run.

## Compound persistence

`SimulationState::save_checkpoint()` publishes one generation with exactly three authoritative sections:

1. World;
2. Weather;
3. Multiresolution Water.

Channel storage is part of the Multiresolution Water section; it is not a fourth persistence authority. Continental topology is derived from World and is rebuilt on load rather than serialized.

Multiresolution-water format v5 keeps the channel-state byte layout introduced by v3. Transport parameters are not serialized: they are re-derived from authoritative topology on create/load. v3 fixed-reservoir and v4 length/slope files therefore preserve their persisted water exactly while migrating to current v5 transport semantics. A valid v2 file loads with zero channel storage because v2 had no persistent in-channel state. Format v1 remains rejected because it predates the current spatial soil-capacity semantics.

The compound save path serializes component sections privately, records byte lengths and FNV-1a corruption checksums, assembles a same-directory publish file, validates the completed container, flushes it, then atomically replaces the target. Failures before publication leave an existing target untouched.

The loader validates container structure/checksums, loads World, rebuilds topology, loads weather/water against that identity, then requires all component clocks to equal the checkpoint global day before exposing state.

FNV-1a is accidental-corruption detection, not cryptographic authentication. Current binary persistence uses native POD representations; cross-endian save-file portability is not guaranteed. POSIX publication is atomic under normal filesystem rename semantics, but full power-loss durability of the directory entry is not claimed because the parent directory is not explicitly fsynced after rename.

See `docs/SIMULATION.md` for the v0.10 lifecycle/container contract and `docs/CHANNEL_TRANSPORT.md` for the current v0.12 channel-state contract.

## C ABI

Existing pre-v0.12 C POD layouts and existing function signatures remain compatible in v0.12.

Additive read-only queries expose one L0 channel volume, total channel storage and derived per-reach transport metadata on:

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
- Channel travel time uses a bounded synthetic daily heuristic from reach length, slope and accumulated discharge; it is not empirically calibrated river celerity.
- The one-L0-edge-per-day scheduler cannot resolve real sub-daily flood-wave propagation even when a physical reach travel time would be much shorter than one day.
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

One GCC Release CI observation with the bounded residence heuristic on the 449,208-L0 / 64-refined fixture measured approximately:

- simulation construction: `857.664 ms`;
- five unified days: `790.442 ms`;
- checkpoint save: `174.015 ms`;
- checkpoint load including topology reconstruction: `971.509 ms`;
- checkpoint size: `21,769,048 bytes` (`~20.76 MiB`);
- persistent channel storage after five warmup days: `85,711,133,025.076 m³`;
- peak benchmark RSS: `266,788 KiB`;
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
- `docs/CHANNEL_TRANSPORT.md` — current v0.12 persistent reach-aware channel ownership, routing and persistence contract.
- `docs/WEATHER.md` — transient weather model.
- `docs/MULTIRESOLUTION_WATER.md` — historical v0.8 conservative coarse/fine terrestrial ownership design with current-status pointers.
- `docs/SOIL.md` — static spatial soil properties and capacity integration.

## Audits

- `docs/AUDIT_v0.1.md` through `docs/AUDIT_v0.8.md` — earlier spatial/hydrology/soil/weather milestones.
- `docs/AUDIT_v0.9.md` — selects unified simulation/checkpoint ownership.
- `docs/AUDIT_v0.10.md` — validates the unified lifecycle and selects persistent channel transport.
- `docs/AUDIT_v0.11.md` — validates conserved channel travel time and the v3 persistence/ABI/checkpoint integration.
- `docs/AUDIT_v0.12.md` — validates reach-aware bounded residence, v5 semantic migration and selects derived L1 atmospheric forcing.

## Next bounded milestone

The channel model is now intentionally good enough for the current daily world scale: reach length dominates a weak bounded slope/discharge heuristic, while conservation and one-edge/day causality remain exact engine contracts.

Further river-routing depth is deferred until gameplay or validation requires sub-daily propagation, observed-gauge calibration, or independent control of hydrograph lag versus attenuation. At that point the correct milestone is a routing-resolution/model change rather than more precision in the current heuristic.
