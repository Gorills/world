# WorldSim v0.10.0 — unified simulation checkpoints

Headless C++20 simulation core for a large persistent world. v0.10 puts persistent world history, transient weather and conserved multiresolution water behind one application-level lifecycle and one compound checkpoint generation.

## Implemented

- Engine-independent C++20 core (`worldsim`).
- C ABI suitable for thin Unity/Godot/Unreal adapters.
- Spatial hierarchy:
  - L0 climate / weather / continental drainage / coarse dynamic water / parent-equivalent soil: 8192 m;
  - L1 regional terrain / authoritative refined drainage / selective detailed dynamic water / spatial soil heterogeneity: 1024 m;
  - L2 local persistent history: 64 m, 16×16 per L1 cell;
  - future entities use continuous coordinates.
- Europe-scale world bounds without eager L1/L2 allocation.
- Deterministic procedural terrain, climate and soil scaffolding.
- Configurable sea-level datum.
- Whole-world authoritative L0 drainage and fixed 8×8 authoritative L1 refinement.
- Capacity-aware dynamic water with conservative coarse/fine ownership.
- Explicit whole-world transient `WeatherState` with coherent daily precipitation, temperature and PET forcing.
- `SimulationState` as the application-level owner of:
  - `World` persistent L2 history;
  - derived continental topology;
  - `WeatherState`;
  - `MultiresolutionWaterState`.
- One exact simulation day shared by weather, coarse water and every refined water tile.
- Runtime refinement, aggregation, daily advance and persistent surface mutation through the unified owner.
- Compound versioned checkpoints containing World + Weather + Multiresolution Water as one validated generation.
- Checkpoint section lengths/checksums, strict corruption/truncation checks and component identity/clock validation.
- Same-directory temporary checkpoint publication with validated atomic replacement of an existing target.
- Migration from existing `World::save()` files while preserving materialized L2 history.
- Opaque `ws_simulation_state` C ABI without mutable component-handle escape.
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

### Water

```text
unrefined parent
    L0 stores = authoritative
    L1 tile   = absent

refined parent
    L0 stores = zero
    L1 tile   = authoritative
```

The same conserved water volume is never independently advanced at both resolutions.

### Atmosphere

Static `ClimateSample` remains the reproducible long-run baseline. `WeatherState` owns transient atmospheric anomaly state. Hydrology consumes precipitation, temperature and PET forcing records rather than embedding atmosphere.

## Conservation

For each global mixed-resolution water day:

```text
storage_before + terrestrial_precipitation
≈ storage_after + terrestrial_ET + terminal_outflow
```

The audited Europe fixture continues to observe a maximum relative daily balance residual of about `5.9e-9` under weather-driven forcing.

## Compound persistence

`SimulationState::save_checkpoint()` publishes one generation with exactly three authoritative sections:

1. World;
2. Weather;
3. Multiresolution Water.

Continental topology is derived from World and is rebuilt on load rather than serialized as another authority.

The save path serializes component sections privately, records byte lengths and FNV-1a corruption checksums, assembles a same-directory publish file, validates the completed container, flushes it, then atomically replaces the target. Failures before publication leave an existing target untouched.

The loader validates container structure/checksums, loads World, rebuilds topology, loads weather/water against that identity, then requires all component clocks to equal the checkpoint global day before exposing state.

FNV-1a is accidental-corruption detection, not cryptographic authentication. Current binary persistence uses native POD representations; cross-endian save-file portability is not yet guaranteed. POSIX publication is atomic under normal filesystem rename semantics, but v0.10 does not claim full power-loss durability of the directory entry because the parent directory is not explicitly fsynced after rename.

See `docs/SIMULATION.md` for the complete contract.

## Legacy World migration

Existing `World::save()` files remain readable and their format is unchanged.

A pre-v0.10 World can become a day-zero unified simulation without losing materialized L2 history:

```cpp
auto world = worldsim::World::load("legacy.ws");
auto simulation = worldsim::SimulationState::from_world(std::move(world));
```

Weather and dynamic water start from their deterministic day-zero state because those transient authorities were not stored inside the legacy World file.

## Scientific/model limitations

- Terrain/climate/soil fields remain synthetic scaffolding, not reconstructed Europe.
- Weather is a coherent stochastic L0 layer, not numerical weather prediction.
- No L1 atmospheric downscaling, pressure, wind, humidity, radiation or explicit cloud physics.
- PET remains a simple temperature proxy.
- Soil remains one vertically aggregated bucket with no lateral groundwater aquifer state.
- Channel routing still moves daily quickflow/baseflow through the drainage DAG within one day; persistent in-channel travel-time/flood-wave state is not yet modeled.
- No wetlands, floodplains, erosion, sediment or vegetation feedback.

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

One GCC Release v0.10 checkpoint observation on the 449,208-L0 / 64-refined fixture measured approximately:

- checkpoint save: `163 ms`;
- checkpoint load including topology reconstruction: `919 ms`;
- checkpoint size: `18,175,376 bytes` (`~17.33 MiB`);
- peak benchmark RSS: `229,872 KiB`.

Timings/RSS are environment-specific observations, not API guarantees.

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
- `docs/SIMULATION.md` — v0.10 lifecycle/checkpoint/C ABI/CLI contract.
- `docs/WEATHER.md` — transient weather model.
- `docs/MULTIRESOLUTION_WATER.md` — conservative coarse/fine water ownership.
- `docs/SOIL.md` — static spatial soil properties and capacity integration.

## Audits

- `docs/AUDIT_v0.1.md` — spatial/persistence foundation before v0.2.
- `docs/AUDIT_v0.2.md` — regional hydrology before v0.3.
- `docs/AUDIT_v0.3.md` — authoritative drainage boundary before v0.4.
- `docs/AUDIT_v0.4.md` — establishes the complete L0 dynamic-water boundary.
- `docs/AUDIT_v0.5.md` — validates coarse water and selects conservative refinement ownership.
- `docs/AUDIT_v0.6.md` — validates ownership and selects spatial soil properties.
- `docs/AUDIT_v0.7.md` — validates soil properties and selects capacity-aware dynamics.
- `docs/AUDIT_v0.8.md` — selects authoritative transient weather.
- `docs/AUDIT_v0.9.md` — full accumulated audit selecting unified simulation/checkpoint ownership.
- `docs/AUDIT_v0.10.md` — validates the unified lifecycle and selects persistent channel travel-time state as the next conserved subsystem boundary.

## Next bounded milestone

With compound ownership/checkpointing established, the next architecture-level hydrology limitation is persistent in-channel travel time. The next slice should keep channel storage inside `MultiresolutionWaterState`, conserve it across L0/L1 routing boundaries, advance it on the same global clock and persist it through the existing Multiresolution Water checkpoint section rather than creating another independently coordinated authority.
