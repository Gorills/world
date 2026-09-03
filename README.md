# WorldSim v0.9.0 — authoritative daily weather

Headless C++20 simulation core for a large persistent world. v0.9 adds an explicit whole-world transient weather state and connects it to the existing authoritative water forcing boundary without changing water ownership.

## Implemented

- Engine-independent C++20 core (`worldsim`).
- C ABI suitable for thin Unity/Godot/Unreal adapters.
- Spatial hierarchy:
  - L0 climate / transient weather / continental drainage / coarse dynamic water / parent-equivalent soil: 8192 m;
  - L1 regional terrain / authoritative refined drainage / selective detailed dynamic water / spatial soil heterogeneity: 1024 m;
  - L2 local persistent history: 64 m, 16×16 per L1 cell;
  - future entities use continuous coordinates.
- Europe-scale world bounds without eager L1/L2 allocation.
- Deterministic procedural terrain, climate and soil scaffolding.
- Configurable sea-level datum.
- Whole-world authoritative L0 drainage and stable basin/outlet topology.
- Fixed 8×8 authoritative L1 refinement with stable cross-tile outlet/ingress edges.
- Authoritative daily L0 weather:
  - exact integer global day;
  - compact temperature and moisture anomaly state;
  - spatially coherent ~32 km synoptic innovations;
  - temporal and neighbor memory;
  - intermittent wet/dry precipitation around the static climate baseline;
  - transient daily temperature and PET;
  - 10-year regression anchoring default precipitation to climate totals.
- Authoritative L0 dynamic water history for every world cell:
  - exact integer global day;
  - snow, surface water, soil water and groundwater;
  - rain/snow, melt, infiltration, ET, percolation, baseflow and runoff;
  - deterministic routing through the continental drainage DAG;
  - whole-world water-balance report.
- Conservative multiresolution ownership:
  - unrefined parent: L0 stores are authoritative;
  - refined parent: L0 stores are zero and sparse 8×8 L1 state is authoritative;
  - actual overlap-area conservation for partial world cells;
  - conservative L1→L0 aggregation;
  - repeated materialize/dematerialize;
  - one exact clock across both ownership modes.
- Coupled mixed-resolution daily routing:
  - coarse upstream → exact refined ingress;
  - refined outlet → coarse downstream;
  - refined-to-refined transfer through deterministic boundary cells;
  - no independent coarse step for a refined parent.
- Atomic weather + water stepping:
  - weather and water must have the same world/grid/day;
  - current-day forcing and next weather state are prepared first;
  - weather commits only after the existing atomic water step succeeds;
  - refined L1 water receives its parent L0 atmospheric forcing.
- Deterministic spatial soil properties:
  - parent-equivalent L0 storage/infiltration scale factors;
  - heterogeneous L1 scale factors;
  - actual-area normalization so the L1 weighted mean reproduces the L0 parent property, including partial boundary parents;
  - queries remain derived and do not materialize or persist L1/L2 state.
- Capacity-aware soil-water dynamics:
  - storage scale applies to soil/field/wilting capacity and initial soil water;
  - infiltration scale applies to infiltration capacity;
  - both standalone L1 and whole-world L0 buckets use the same scaling contract;
  - local-capacity state is validated before mutation.
- Saturation-preserving L0→L1 soil-water transfer under heterogeneous child capacities.
- Separate versioned persistence for transient weather and dynamic multiresolution water.
- Existing `World::save()` remains v2 with v1 read compatibility.
- Additive C ABI surfaces for weather, multiresolution water and soil sampling; existing water POD layouts are unchanged.
- Legacy smooth climatological forcing helpers remain available for controlled/reproducible tests and older CLI paths.
- Lazy persistent L2 materialization and `disturb_surface()`.
- PR CI: GCC/Clang warnings-as-errors plus ASan/UBSan.
- Europe-scale water and weather+water benchmark executables.

## Climate vs weather

`ClimateSample` is static long-run world truth derived from seed and coordinates. `WeatherState` is explicit transient state.

```text
static climate baseline
        ↓
seasonal cycle
        +
transient spatial weather anomalies
        ↓
precipitation / temperature / PET forcing
        ↓
authoritative water state
```

Weather owns no water. Water owns no atmosphere.

Daily innovations are generated on a coarser synoptic lattice and interpolated to L0 cells, then combined with autoregressive and neighbor memory. This avoids independent checkerboard noise while keeping the persistent atmospheric state compact.

The default storm multiplier is calibrated by a 10-year regression. The current regression fixture gives a generated/climatological precipitation ratio of approximately `0.9997`, while retaining a mean wet-area fraction of approximately `0.667`.

## Weather/water clock invariant

Coupled weather and water must share one exact day:

```text
weather.day == water.day
```

The coupled helper first prepares the full current-day forcing and next weather state. It then advances water. Weather is committed only after the water step succeeds.

This prevents a rejected hydrology step from leaving the atmosphere one day ahead of the water system.

## Soil capacity invariant

`SoilProperties` contains dimensionless storage-capacity and infiltration-capacity modifiers. For every climate parent, the directly sampled L0 storage scale is the area-weighted mean of its in-world 8×8 L1 child scales:

```text
parent_scale = Σ(child_scale × child_overlap_area)
               --------------------------------------
                    Σ(child_overlap_area)
```

This remains true for partial parents at configured world boundaries. Because soil capacity is the reference capacity multiplied by that scale, the same relation holds for capacity itself.

The values are reproducible from seed + coordinates and are not persisted. The current soil field is synthetic scaffolding, not measured soil classes or reconstructed pedology.

## Capacity-aware refinement

When a coarse parent becomes detailed, soil depth is not copied uniformly. v0.8+ preserves saturation:

```text
parent_saturation = parent_soil_water / parent_soil_capacity
child_soil_water  = parent_saturation × child_soil_capacity
```

Under the parent/child capacity invariant this conserves total soil-water volume over actual world overlap area and keeps every valid child within its capacity.

Snow, surface water and groundwater still transfer by parent depth because no spatial capacity field applies to those stores yet.

## Water ownership invariant

The same water volume is never independently owned by both L0 and L1.

```text
unrefined parent
    L0 stores = authoritative
    L1 tile   = absent

refined parent
    L0 stores = zero
    L1 tile   = authoritative
```

This gives the world one dynamic-water state with variable resolution rather than separate coarse and detailed simulations that can drift.

## Mixed-resolution routing

```text
coarse upstream
      ↓
refined ingress child
      ↓
8×8 authoritative L1 routing
      ↓
refined outlet
      ↓
coarse downstream
```

The continental topological order determines when each parent is processed. Upstream channel volume reaches a refined ingress before that refined parent is stepped, and its outlet is forwarded exactly once.

v0.9 changes the atmospheric forcing reaching these buckets, not the routing topology or conserved-water ownership.

## Conservation

For each global mixed-resolution day:

```text
storage_before + terrestrial_precipitation
≈ storage_after + terrestrial_ET + terminal_outflow
```

The balance includes coarse-owned L0 stores plus refined-owned L1 stores, never both independent copies of the same parent water.

L0↔L1 transfer uses actual in-world overlap areas. Soil water uses saturation-preserving heterogeneous capacity transfer; the other conserved stores retain their existing volume-conserving depth transfer.

The weather-driven Europe benchmark remains inside the same `1e-6` relative conservation gate; the calibrated v0.9 CI observation produced a maximum relative residual of about `5.9e-9` over 30 coupled days.

## Persistence

`World::save()` continues to store world configuration and persistent L2 patches using the existing v1/v2 format. Soil properties and static climate remain derived world truth.

Dynamic multiresolution water remains a separate explicit simulation state. v0.8+ water files use format v2 and reject the old uniform-capacity format v1.

Transient weather is also explicit and uses a separate weather format v1 containing:

- world identity;
- weather-process parameters;
- exact global day;
- L0 raster metadata;
- temperature and moisture anomaly state.

Derived climate/elevation metadata is reconstructed from `World` on weather load.

Weather and water saves are intentionally separate files in v0.9. Applications requiring an atomic disk checkpoint across both must coordinate the two writes externally.

## Scientific/model limitations

v0.9 adds coherent transient forcing; it is still synthetic simulation scaffolding rather than numerical weather prediction or reconstructed Europe.

- Terrain/climate/soil fields are synthetic.
- Weather has no explicit pressure, wind, humidity, cloud or radiation physics.
- PET remains temperature-based.
- L1 water currently receives parent L0 weather; no orographic/sub-grid atmospheric downscaling exists yet.
- Soil remains one vertically aggregated bucket with synthetic modifiers.
- Infiltration remains a bounded bucket flux rather than unsaturated-flow physics.
- L0 routing moves daily quickflow/baseflow through the DAG within one daily step; channel travel time and flood-wave hydraulics are not modeled.
- No lateral groundwater aquifers, wetlands, floodplains, channel geometry, erosion, sediment or vegetation feedback yet.

See `docs/WEATHER.md`, `docs/SOIL.md`, `docs/MULTIRESOLUTION_WATER.md`, `docs/CONTINENTAL_WATER.md`, `docs/AUDIT_v0.8.md` and the earlier audit documents.

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

One calibrated GCC Release CI observation for 449,208 L0 cells, 64 refined water parents and 30 coupled days measured approximately:

- 205 ms to create weather state;
- 129 ms per coupled weather+water day;
- 136 MiB peak RSS;
- `5.9e-9` maximum relative water-balance residual.

Benchmark timings/RSS are environment-specific observations, not API guarantees.

## CLI

Create a demo save:

```bash
./build/worldsim_cli demo demo.ws
```

Authoritative whole-world drainage:

```bash
./build/worldsim_cli continent demo.ws 25
```

Run the legacy smooth-forcing coarse water history:

```bash
./build/worldsim_cli continental-water demo.ws 365
```

Run authoritative transient weather coupled to whole-world water:

```bash
./build/worldsim_cli weather-water demo.ws 365
```

Run the older standalone detailed L1 solver for one authoritative tile:

```bash
./build/worldsim_cli watercycle demo.ws 3 4 365
```

Selective refined ownership, weather persistence and engine integration are exposed through the C++ APIs and dedicated C ABI extensions.

## Audits

- `docs/AUDIT_v0.1.md` — spatial/persistence foundation before v0.2.
- `docs/AUDIT_v0.2.md` — regional hydrology before v0.3.
- `docs/AUDIT_v0.3.md` — authoritative drainage boundary before v0.4.
- `docs/AUDIT_v0.4.md` — establishes the v0.5 L0 state boundary.
- `docs/AUDIT_v0.5.md` — selects the v0.6 ownership model.
- `docs/AUDIT_v0.6.md` — validates ownership and selects the v0.7 spatial-property contract.
- `docs/AUDIT_v0.7.md` — selects capacity-aware water integration.
- `docs/AUDIT_v0.8.md` — validates capacity-aware ownership and selects authoritative weather before channel travel-time state.

## Next bounded milestone

Re-audit v0.9 before selecting implementation scope. The strongest remaining hydrologic architecture limitation is expected to be persistent channel travel-time/routing state, but it should be designed as conserved state rather than patched into the existing same-day DAG routing. L1 weather downscaling and richer atmospheric physics remain separate candidates.
