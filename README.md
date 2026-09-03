# WorldSim v0.9.0 — authoritative daily weather

Headless C++20 simulation core for a large persistent world. v0.9 adds an explicit whole-world transient atmosphere while preserving the v0.8 conservative L0/L1 water ownership and soil-capacity contracts.

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
- Whole-world authoritative L0 drainage and stable basin/outlet topology.
- Fixed 8×8 authoritative L1 refinement with stable cross-tile outlet/ingress edges.
- Authoritative L0 dynamic water history for every world cell:
  - exact integer global day;
  - snow, surface water, soil water and groundwater;
  - rain/snow, melt, infiltration, ET, percolation, baseflow and runoff;
  - deterministic routing through the continental drainage DAG;
  - whole-world water-balance report.
- Conservative multiresolution water ownership with sparse detailed L1 refinement.
- Deterministic spatial soil properties and capacity-aware water buckets.
- Saturation-preserving L0→L1 soil-water transfer under heterogeneous child capacities.
- Explicit whole-world L0 `WeatherState`:
  - exact integer weather day;
  - persistent temperature and moisture anomalies;
  - spatially coherent ~32 km synoptic innovations;
  - temporal and neighbor memory;
  - intermittent precipitation anchored to static climate over long runs;
  - transient mean air temperature and PET forcing.
- Stable weather→hydrology forcing boundary: water still consumes precipitation, temperature and PET rather than owning atmosphere.
- Atomic weather + continental-water and weather + multiresolution-water daily helpers with exact clock alignment.
- Separate versioned persistence for weather and multiresolution water; existing `World::save()` remains compatible with its prior formats.
- Additive weather, multiresolution-water and soil C ABI extensions; existing water POD layouts remain unchanged.
- CLI `weather-water` command in addition to legacy smooth-forcing commands.
- Regression coverage for weather determinism/coherence, 10-year climate anchoring across multiple world identities, coupled atomicity, persistence and C ABI behavior.
- CI: GCC/Clang warnings-as-errors, ASan/UBSan, MSVC static smoke and Windows shared-library consumers.
- Europe-scale water and weather+water benchmark executables.

## Core ownership invariants

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

Static `ClimateSample` is the reproducible long-run baseline. `WeatherState` owns transient atmospheric anomaly state only. Hydrology consumes resulting forcing records and does not mutate atmosphere.

### Time

Authoritative weather and multiresolution water each carry an exact integer day. Coupled stepping requires equality before the step and advances both by exactly one day.

## Weather calibration

The initial intermittent-storm formulation was rejected by regression because it generated only about `0.646×` the static climate precipitation target over the 10-year calibration fixture.

Only the linear storm-intensity multiplier was recalibrated, leaving wet/dry frequency and anomaly dynamics unchanged. The original 10-year fixture is now approximately `0.9997×` its climatological precipitation target. A second 10-year regression matrix covers several seeds and partial/misaligned world bounds so the default is not accepted from one identity alone.

This validates consistency with WorldSim's synthetic climate field; it is not a claim that the generated weather reproduces observed European statistics.

## Conservation

For each global mixed-resolution water day:

```text
storage_before + terrestrial_precipitation
≈ storage_after + terrestrial_ET + terminal_outflow
```

The balance includes coarse-owned L0 stores plus refined-owned L1 stores, never two copies of the same parent water.

On the audited Europe fixture the observed maximum relative daily balance residual remains about `5.9e-9` under weather-driven forcing.

## Persistence

World, weather and multiresolution water are explicit state authorities with separate versioned files.

- `World::save()` persists world configuration and materialized L2 history.
- weather persistence stores exact day, weather parameters and L0 anomaly state.
- multiresolution-water persistence stores exact day, coarse stores and sparse refined ownership.

Loaders reject wrong-world identity, malformed/truncated state and inconsistent local validity according to their formats.

v0.9 does **not** yet provide one transactional compound checkpoint across all three files. A process failure between independent saves can leave different generations; coupled runtime APIs detect clock/identity mismatches but do not automatically recover the previous complete generation. The v0.9 audit selects a unified simulation/checkpoint owner as the next major dependency before adding another persistent conserved subsystem such as channel travel-time storage.

Current binary formats use native POD representations; portable cross-endian save files are not yet a guarantee.

## Scientific/model limitations

- Terrain/climate/soil fields remain synthetic scaffolding, not reconstructed Europe.
- Weather is a coherent stochastic L0 layer, not numerical weather prediction.
- No L1 atmospheric downscaling, pressure, wind, humidity, radiation or explicit cloud physics.
- PET remains a simple temperature proxy.
- Soil remains one vertically aggregated bucket.
- No lateral groundwater aquifer state.
- Channel routing still moves daily quickflow/baseflow through the DAG within one day; persistent travel time/flood-wave hydraulics are not modeled.
- No wetlands, floodplains, erosion, sediment or vegetation feedback.

See `docs/WEATHER.md`, `docs/MULTIRESOLUTION_WATER.md`, `docs/SOIL.md`, `docs/ARCHITECTURE.md` and `docs/AUDIT_v0.9.md`.

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
```

Reported timings and RSS are environment-specific observations, not API guarantees.

## CLI

Create a demo save:

```bash
./build/worldsim_cli demo demo.ws
```

Authoritative whole-world drainage:

```bash
./build/worldsim_cli continent demo.ws 25
```

Legacy deterministic coarse water forcing:

```bash
./build/worldsim_cli continental-water demo.ws 365
```

Weather-driven coarse water:

```bash
./build/worldsim_cli weather-water demo.ws 365
```

Standalone detailed L1 solver:

```bash
./build/worldsim_cli watercycle demo.ws 3 4 365
```

## Audits

- `docs/AUDIT_v0.1.md` — spatial/persistence foundation before v0.2.
- `docs/AUDIT_v0.2.md` — regional hydrology before v0.3.
- `docs/AUDIT_v0.3.md` — authoritative drainage boundary before v0.4.
- `docs/AUDIT_v0.4.md` — rejects an L1-only scheduler and establishes the v0.5 L0 state boundary.
- `docs/AUDIT_v0.5.md` — validates the coarse boundary and selects v0.6 ownership.
- `docs/AUDIT_v0.6.md` — validates ownership and selects v0.7 spatial properties.
- `docs/AUDIT_v0.7.md` — validates soil properties and selects capacity-aware water integration.
- `docs/AUDIT_v0.8.md` — selects authoritative transient weather over channel hydraulics.
- `docs/AUDIT_v0.9.md` — full accumulated audit; selects unified simulation ownership/checkpointing before adding another dynamic state authority.

## Next bounded milestone

v0.10 should establish one `SimulationState` lifecycle and compound checkpoint for persistent world history + weather + multiresolution water. Channel travel-time/flood-wave state should be added only after that ownership/persistence boundary exists.
