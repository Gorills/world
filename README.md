# WorldSim v0.14.0 — sparse persistent L2 vegetation

Headless C++20 simulation core for a large persistent world. v0.14 adds disturbance-aware vegetation recovery to already-materialized 64 m L2 history while keeping whole-world weather/water compact and avoiding eager ecology allocation.

## Implemented

- Engine-independent C++20 core (`worldsim`).
- C ABI suitable for thin Unity/Godot/Unreal adapters.
- Spatial hierarchy:
  - L0 climate / weather / continental drainage / coarse dynamic water / persistent channel storage / parent-equivalent soil: 8192 m;
  - L1 regional terrain / authoritative refined drainage / selective detailed dynamic water / spatial soil heterogeneity: 1024 m;
  - L2 local persistent history: 64 m, 16×16 per L1 cell, including disturbance and live vegetation biomass;
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
- Stateless L0→L1 forcing for refined water: bounded elevation-lapse temperature, temperature-derived PET and area-normalized terrain precipitation redistribution that preserves parent precipitation volume to float forcing precision.
- Sparse persistent L2 vegetation biomass initialized from static forest potential, immediately damaged by surface disturbance and recovered daily from current temperature + authoritative soil saturation.
- `SimulationState` as the application-level owner of:
  - `World` persistent L2 disturbance + vegetation history;
  - derived continental topology;
  - `WeatherState`;
  - `MultiresolutionWaterState`, including channel storage.
- One exact simulation day shared by weather, coarse water and every refined water tile.
- Runtime refinement, aggregation, vegetation-aware daily advance and persistent surface mutation through the unified owner.
- Compound versioned checkpoints containing World (including sparse vegetation), Weather + Multiresolution Water as one validated generation.
- Multiresolution-water persistence v6 with explicit v2-v5 semantic migration and no duplicated persisted transport/forcing metadata.
- Checkpoint section lengths/checksums, strict corruption/truncation checks and component identity/clock validation.
- Same-directory temporary checkpoint publication with validated atomic replacement of an existing target.
- World persistence v3 with deterministic v1/v2 migration of existing local disturbance history into vegetation biomass.
- Migration from existing `World::save()` files while preserving materialized L2 history.
- Additive channel/forcing/vegetation C ABI surfaces without changing existing pre-v0.14 POD layouts or function signatures.
- CLI `simulation-run` and `simulation-resume` paths.
- GCC/Clang warnings-as-errors, ASan/UBSan, and complete MSVC shared-library consumer tests.
- Europe-scale water, weather+water and unified checkpoint benchmarks in GCC CI.

## Core ownership invariants

### Simulation lifecycle

```text
SimulationState
├── World                      authoritative sparse L2 disturbance + vegetation history
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

Vegetation forcing and the next sparse L2 history are staged from current-day weather/water first. Authoritative water/weather then advance atomically; only after that succeeds is staged local vegetation history committed with a no-throw swap. A rejected environmental step therefore cannot partially advance vegetation.

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

Static `ClimateSample` remains the reproducible long-run baseline. `WeatherState` owns transient atmospheric anomaly state at L0. Refined L1 hydrology derives child forcing on demand from the parent L0 record plus authoritative L1 terrain; the derived records are diagnostics, not another atmospheric state authority.

### Sparse local vegetation

`forest_potential` is deterministic static carrying potential. Materialized L2 cells additionally persist `disturbance` and `vegetation_biomass`.

```text
disturbance_next = disturbance × exp(-1 / 730)
target_biomass   = forest_potential × (1 - disturbance_next)

temperature_factor = clamp((T + 2) / 18, 0, 1)
moisture_factor    = clamp((soil_saturation - 0.1) / 0.6, 0, 1)
recovery_fraction  = 1 - exp(-(temperature_factor × moisture_factor) / 365)

biomass_next =
    biomass + (target_biomass - biomass) × recovery_fraction
```

Only already-materialized L2 patches advance. Surface disturbance immediately clamps biomass to the new disturbed carrying capacity. This is a live-cover/recovery proxy, not species succession or a carbon model.

## Conservation

For each global mixed-resolution water day:

```text
storage_before + terrestrial_precipitation
≈ storage_after + terrestrial_ET + terminal_outflow
```

`storage_before` and `storage_after` include terrestrial coarse/refined stores plus all persistent L0 channel storage.

The v0.14 Europe fixture observes a maximum relative daily water-balance residual of `5.886e-9` on the audited GCC Release run while also advancing 16,384 persistent L2 vegetation cells.

## Compound persistence

`SimulationState::save_checkpoint()` publishes one generation with exactly three authoritative sections:

1. World;
2. Weather;
3. Multiresolution Water.

Channel storage is part of the Multiresolution Water section; it is not a fourth persistence authority. Vegetation is part of the existing World L2 section; it is not a fourth simulation/checkpoint component. Continental topology is derived from World and is rebuilt on load rather than serialized.

World format v3 adds one persistent vegetation-biomass float per materialized L2 cell. v1/v2 local history preserves existing fields and derives initial biomass as disturbed forest potential on in-world land cells. Multiresolution-water format v6 remains unchanged: channel transport and refined atmospheric forcing are derived rather than serialized.

The compound save path serializes component sections privately, records byte lengths and FNV-1a corruption checksums, assembles a same-directory publish file, validates the completed container, flushes it, then atomically replaces the target. Failures before publication leave an existing target untouched.

The loader validates container structure/checksums, loads World, rebuilds topology, loads weather/water against that identity, then requires all component clocks to equal the checkpoint global day before exposing state.

FNV-1a is accidental-corruption detection, not cryptographic authentication. Current binary persistence uses native POD representations; cross-endian save-file portability is not guaranteed. POSIX publication is atomic under normal filesystem rename semantics, but full power-loss durability of the directory entry is not claimed because the parent directory is not explicitly fsynced after rename.

See `docs/SIMULATION.md` for the v0.10 lifecycle/container contract and `docs/CHANNEL_TRANSPORT.md` for the current v0.12 channel-state contract.

## C ABI

Existing pre-v0.14 C POD layouts and existing function signatures remain compatible in v0.14.

Additive vegetation structs/functions expose local biomass, explicit standalone vegetation stepping and a `ws_simulation_advance_day_v2` report containing environment + vegetation metrics. The old `ws_simulation_advance_day` signature remains unchanged and still advances vegetation as part of the unified generation.

Existing channel and refined-forcing query surfaces remain unchanged.

## Legacy World migration

Existing `World::save()` v1/v2 files remain readable. New saves use World format v3 because persistent L2 vegetation biomass is now part of local history.

A pre-v0.10 World can become a day-zero unified simulation without losing materialized L2 history:

```cpp
auto world = worldsim::World::load("legacy.ws");
auto simulation = worldsim::SimulationState::from_world(std::move(world));
```

Weather, terrestrial dynamic water and channel storage start from their deterministic day-zero state because those transient authorities were not stored inside the legacy World file.

## Scientific/model limitations

- Terrain/climate/soil fields remain synthetic scaffolding, not reconstructed Europe.
- Weather is a coherent stochastic L0 layer, not numerical weather prediction.
- No persistent L1 atmospheric state, pressure, wind, humidity, radiation or explicit cloud physics.
- L1 precipitation redistribution is a bounded terrain heuristic, not a physical windward/leeward orographic model.
- PET remains a simple temperature proxy, recalculated from the derived child temperature.
- Soil remains one vertically aggregated bucket with no lateral groundwater aquifer state.
- Channel travel time uses a bounded synthetic daily heuristic from reach length, slope and accumulated discharge; it is not empirically calibrated river celerity.
- The one-L0-edge-per-day scheduler cannot resolve real sub-daily flood-wave propagation even when a physical reach travel time would be much shorter than one day.
- No channel capacity, flood-wave/backwater hydraulics, wetlands or floodplains.
- Vegetation is one normalized live-cover/recovery proxy: no species, age structure, succession, seed dispersal, fire, nutrients, carbon pools or vegetation→hydrology feedback.
- No erosion or sediment feedback.

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

One GCC Release CI observation with v0.14 sparse vegetation on the 449,208-L0 / 64-refined fixture measured approximately:

- 64 materialized vegetation patches / 16,384 disturbed L2 cells;
- simulation construction: `671.042 ms`;
- materialize 64 refined parents: `10.747 ms`;
- five unified environment + vegetation days: `739.681 ms`;
- checkpoint save: `386.885 ms`;
- checkpoint load including topology reconstruction: `751.586 ms`;
- checkpoint size: `22,093,640 bytes` (`~21.07 MiB`);
- persistent channel storage after five warmup days: `85,711,133,025.076 m³`;
- vegetation biomass-area after warmup: `12,098,057.493 m²`;
- vegetation disturbance-area after warmup: `33,325,391.497 m²`;
- peak benchmark RSS: `270,440 KiB`;
- maximum relative water-balance residual: `5.886e-9`.

The benchmark requires exact channel equality across every L0 cell and exact equality of all 64 vegetation patches after checkpoint reload and after one deterministic future day. Timings/RSS are environment-specific observations, not API guarantees.

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
- `docs/WEATHER.md` — L0 transient weather model and v0.13 stateless L1 forcing transform.
- `docs/VEGETATION.md` — v0.14 sparse L2 biomass, disturbance/recovery and persistence contract.
- `docs/MULTIRESOLUTION_WATER.md` — historical v0.8 conservative coarse/fine terrestrial ownership design with current-status pointers.
- `docs/SOIL.md` — static spatial soil properties and capacity integration.

## Audits

- `docs/AUDIT_v0.1.md` through `docs/AUDIT_v0.8.md` — earlier spatial/hydrology/soil/weather milestones.
- `docs/AUDIT_v0.9.md` — selects unified simulation/checkpoint ownership.
- `docs/AUDIT_v0.10.md` — validates the unified lifecycle and selects persistent channel transport.
- `docs/AUDIT_v0.11.md` — validates conserved channel travel time and the v3 persistence/ABI/checkpoint integration.
- `docs/AUDIT_v0.12.md` — validates reach-aware bounded residence, v5 semantic migration and selects derived L1 atmospheric forcing.
- `docs/AUDIT_v0.13.md` — validates stateless conservative L1 forcing and v6 semantic migration.
- `docs/AUDIT_v0.14.md` — validates sparse persistent vegetation, World v3 migration and unified checkpoint evolution.

## Next bounded milestone

v0.14 establishes the first persistent ecology-facing state without making ecology global/eager. Further plant physics is deferred until a concrete consumer needs it.

The next strongest product-level gap is now the entity/settlement layer that can consume terrain, weather, water, disturbance and vegetation. Deeper hydrology or richer ecology should be driven by requirements from that layer rather than added by default.
