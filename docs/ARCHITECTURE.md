# Architecture decisions — v0.10

## 1. Resolution hierarchy

| Level | Resolution | Purpose | Storage |
|---|---:|---|---|
| L0 | 8192 m | static climate baseline + transient weather + authoritative continental drainage + default dynamic water history + parent-equivalent soil | derived topology/properties + compact weather/water state |
| L1 | 1024 m | regional terrain, authoritative drainage refinement + selectively authoritative detailed water + spatial soil heterogeneity | topology/properties derived; dynamic water sparse |
| L2 | 64 m | local persistent environmental history | lazy persistent |
| Entity | continuous | people/animals/items/buildings later | future |

The hierarchy remains fixed. Configurability is deferred until a concrete requirement outweighs the persistence and cross-level complexity.

## 2. Unified runtime ownership

v0.10 adds the application-level lifecycle boundary that previous milestones deliberately lacked:

```text
SimulationState
├── World                      authoritative persistent L2 history
├── Continental topology       derived from World
├── WeatherState               authoritative transient atmosphere
└── MultiresolutionWaterState  authoritative conserved water
```

`SimulationState` exposes const component views. Its application-level mutation surface is deliberately small:

- one-day coupled advance;
- water refinement;
- water aggregation;
- persistent surface disturbance;
- compound checkpoint save/load.

Lower-level standalone APIs remain available for focused solvers, tests and compatibility, but a normal evolving-world application no longer needs to coordinate weather/water generations itself.

Continental topology is derived from `World`; it is not another mutable or serialized authority.

## 3. World truth vs materialization

The whole world has one truth but does not need one resolution everywhere.

- Static base fields can be reproduced from seed + coordinates.
- Time-dependent state that must exist everywhere is kept cheaply at L0 unless a subsystem requires sparse refinement.
- Persistent or active fine detail exists only where history requires it.
- Querying climate, terrain, weather samples or soil properties does not materialize L1/L2 history.
- Materializing dynamic water changes conserved ownership and is observable simulation state.
- Weather is explicit dynamic state but remains L0-only.
- External callers do not receive mutable C++ container access through the unified owner.
- Derived analyses are not promoted to authoritative state if they can be reconstructed from authoritative inputs.

The milestone sequence is now:

- v0.3: authoritative whole-world drainage topology;
- v0.4: standalone detailed L1 water;
- v0.5: complete L0 water history + global day;
- v0.6: conservative sparse L1 water ownership;
- v0.7: deterministic spatial soil properties;
- v0.8: capacity-aware water;
- v0.9: authoritative transient weather;
- v0.10: unified runtime ownership + compound checkpoint generation.

## 4. Static climate vs transient weather

Static `ClimateSample` remains reproducible long-run world truth and provides mean temperature, annual precipitation and continentality.

`WeatherState` owns transient daily anomalies:

```text
temperature_anomaly_c
moisture_anomaly
```

Daily forcing combines static climate, deterministic seasonality, transient anomalies and coherent storm intermittency. Hydrology consumes precipitation, temperature and PET through the established forcing boundary.

Weather owns atmosphere; water owns conserved water. `SimulationState` owns their lifecycle, not their internal physics.

## 5. Weather coherence and calibration

Independent random values per 8192 m cell would create checkerboard forcing. Weather innovations are therefore evaluated on a 4×4-L0 synoptic lattice (~32 km at the fixed hierarchy) and interpolated to L0 cells.

Transient anomalies combine:

- previous-day autoregressive persistence;
- bounded four-neighbor memory;
- spatially coherent daily innovations.

Storm intermittency uses persistent moisture anomaly plus a coherent daily storm innovation. Long-run regression anchors generated precipitation to static climate. The initial intermittent model produced roughly `0.646×` climatology; only the linear storm-intensity multiplier was recalibrated, bringing the original 10-year fixture to approximately `0.9997×` while retaining wet/dry frequency behavior.

This remains synthetic stochastic weather scaffolding, not numerical weather prediction.

## 6. Soil property truth and effective buckets

`SoilProperties` contains positive dimensionless storage-capacity and infiltration-capacity modifiers.

Effective local bucket parameters are:

```text
soil_capacity         = reference_soil_capacity         × storage_scale
field_capacity        = reference_field_capacity        × storage_scale
wilting_point         = reference_wilting_point         × storage_scale
initial_soil_water    = reference_initial_soil_water    × storage_scale
infiltration_capacity = reference_infiltration_capacity × infiltration_scale
```

L1 heterogeneity is normalized by actual world-overlap area so its area-weighted parent equivalent reproduces the direct L0 property, including partial boundary parents. Soil properties are derived static truth and are not serialized.

## 7. Water ownership and conservation

Dynamic conserved water cannot exist as two independently advancing copies.

```text
coarse-owned:
    L0 parent stores authoritative
    L1 dynamic state absent

refined-owned:
    L0 parent stores zero
    L1 8×8 stores authoritative
```

Materialization and aggregation conserve snow water equivalent, surface water, soil water and groundwater using actual overlap area.

Soil refinement preserves parent saturation because child capacities differ:

```text
parent_saturation = parent_soil_water / parent_soil_capacity
child_soil_water  = parent_saturation × child_soil_capacity
```

The parent-equivalent capacity invariant makes the transfer volume-conservative. Snow, surface water and groundwater still transfer by parent depth because no spatial capacity field currently applies to those stores.

## 8. Coordinates and indexing

World positions are double-precision meters. Grid coordinates are signed 64-bit integers with mathematical floor semantics.

World bounds are limited by the floating-point precision required by 64 m L2 cells rather than by the formal int64 range.

Raster indexing rejects extreme out-of-range coordinates without signed-overflowing subtraction. Continental water, refined water, standalone detailed hydrology and weather use checked bounds/representability before indexing.

## 9. Hydrology topology

Water topology includes raster and graph semantics:

- per-cell terrain/fill elevation, local yield, accumulated discharge and downstream coordinate;
- river edges;
- lake records with downstream connectivity;
- stable cross-tile ingress/outlet edges for L1 refinement.

The whole-world L0 drainage DAG is the parent ordering/connection truth. A refined parent replaces local bucket/routing interior state without replacing the parent L0 downstream relation.

Authoritative refinement inherits the coarse parent's ocean classification for all active L1 children; the current model does not create mixed land/ocean ownership within a single L0 parent.

## 10. Global time and scheduling

The unified invariant is:

```text
SimulationState.day
== WeatherState.day
== MultiresolutionWaterState.day
== every refined tile day
```

One day is advanced as:

```text
prepare current-day weather forcing
prepare next WeatherState in scratch storage
        ↓
advance authoritative coarse/refined water atomically
        ↓
commit next WeatherState
```

A rejected hydrology input cannot advance weather independently.

Inside water, the L0 topological order remains:

```text
unrefined parent
    → capacity-aware coarse bucket
    → coarse route

refined parent
    ← upstream channel ingress
    → capacity-aware detailed 8×8 L1 bucket/routing
    → one external outlet volume
    → parent L0 downstream relation
```

Refined L1 water currently receives its parent L0 atmospheric forcing. No L1 weather downscaling is invented without a separate elevation/orographic/sub-grid contract.

## 11. Forcing boundary and compatibility helpers

Hydrology consumes precipitation, mean air temperature and potential evapotranspiration.

`make_weather_daily_forcing()` exposes authoritative current-day weather through that boundary. Legacy smooth climatological helpers remain available for controlled tests and old focused CLI/API paths.

PET remains a simple temperature-driven approximation because the atmosphere does not yet carry radiation, humidity or wind.

## 12. Determinism

Terrain/climate/soil/topology use deterministic hashing and tie-breaking. Continental routing order is deterministic. Weather innovations are deterministic for world seed + coordinate + integer day.

The unified checkpoint regression verifies:

- byte-for-byte canonical serialization for identical simulation state;
- exact weather/coarse/refined reload state;
- exact deterministic next-day evolution after reload.

Strict bit-identical floating-point results across all compilers/architectures are still not a formal public portability contract.

## 13. Persistence and compound checkpoints

The component formats remain versioned independently:

### World

`World::save()` v2 stores world configuration and materialized L2 history and retains v1 read compatibility. Static terrain/climate/soil remain derived.

### Multiresolution water

Format v2 stores world identity, hydrology parameters, exact day, complete L0 state and sparse refined ownership/state. Local soil capacities are re-derived from World identity.

### Weather

Format v1 stores world identity, process parameters, exact day, raster metadata and per-cell anomaly state. Climate/elevation metadata is reconstructed.

### Simulation checkpoint

v0.10 adds one container generation with fixed sections:

```text
World
Weather
Multiresolution Water
```

Topology is rebuilt from the World section rather than serialized.

The container stores one global day plus section identifiers, sizes and FNV-1a checksums. On save, the three existing component serializers write private temporary files; a complete same-directory publish file is assembled, revalidated, flushed and atomically renamed/replaced.

The loader validates sizes/checksums, loads World, derives topology, loads weather/water against that identity, then requires the checkpoint global day to match both component clocks before returning `SimulationState`.

This prevents ordinary process failure during multi-file saving from exposing a mixed weather/water/world generation through the unified API.

FNV-1a is accidental-corruption detection, not cryptographic authentication. Native-POD binary encoding remains the existing format assumption. On POSIX the publish file is fsynced before rename, but the parent directory is not explicitly fsynced afterward; v0.10 therefore does not claim full power-loss durability for directory metadata.

## 14. Engine boundary

The C ABI remains opaque-handle + POD-copy based so bindings do not depend on C++ ABI/STL layout.

v0.10 adds `ws_simulation_state`, which owns the unified authority and exposes:

- creation/destruction;
- global-day and ownership counts;
- const-style regional/weather/water copies;
- day advance;
- refinement/aggregation;
- persistent disturbance;
- compound checkpoint save/load;
- a simulation-specific error channel.

It intentionally does not expose mutable internal weather/water handles from the unified owner.

Older standalone World/weather/water C ABIs remain compatible for focused usage.

## 15. Migration and CLI lifecycle

`SimulationState::from_world(World)` is the migration boundary from pre-v0.10 World saves. It preserves exact World configuration/materialized L2 history and creates aligned day-zero weather/water state derived from that World.

CLI lifecycle:

```text
legacy .ws
  └─ simulation-run → compound .wsc
                         └─ simulation-resume → atomically replaced .wsc
```

CTest exercises the `demo → simulation-run → simulation-resume` chain on Linux, sanitizers and MSVC shared-library builds in addition to direct C++/C ABI regressions.

## 16. Scaling

Whole-world weather and water remain compact L0 authorities; L1 water exists only for selected refined parents and L2 persistent history remains lazy.

The v0.10 Europe checkpoint CI fixture uses:

- 449,208 L0 cells;
- 64 refined water parents;
- persistent L2 history;
- five warmup unified days before checkpoint;
- exact next-day equivalence after reload.

One GCC Release observation measured approximately:

- unified simulation construction: `818 ms`;
- materialize 64 parents: `13.8 ms`;
- five unified days: `821 ms`;
- checkpoint save: `163 ms`;
- checkpoint load including topology reconstruction: `919 ms`;
- checkpoint size: `18,175,376 bytes` (`~17.33 MiB`);
- peak RSS: `229,872 KiB`;
- maximum relative water-balance residual: `5.895e-9`.

These are CI observations, not API/performance guarantees.

## 17. Current strongest limitation

With runtime ownership and compound persistence no longer blocking another conserved subsystem, the strongest hydrologic architecture limitation is the same-day channel-routing assumption. Daily quickflow/baseflow can traverse the L0 drainage DAG in one step; there is no persistent in-channel volume or travel-time/flood-wave state.

The next channel-routing model should therefore be designed as conserved dynamic state that:

- shares the unified global clock;
- has explicit L0/L1 ownership semantics;
- participates in mass balance;
- survives refinement/aggregation without double ownership;
- becomes a versioned section of the compound checkpoint.

Other deferred material areas remain L1 atmospheric downscaling, humidity/radiation/wind, lateral groundwater, a multi-layer soil model, wetlands/floodplains and geomorphic/vegetation feedback.
