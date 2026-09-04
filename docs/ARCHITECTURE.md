# Architecture decisions — v0.12

## 1. Resolution hierarchy

| Level | Resolution | Purpose | Storage |
|---|---:|---|---|
| L0 | 8192 m | static climate baseline + transient weather + authoritative continental drainage + default dynamic water history + persistent channel transport + parent-equivalent soil | derived topology/properties + compact weather/terrestrial/channel water state |
| L1 | 1024 m | regional terrain, authoritative drainage refinement + selectively authoritative detailed terrestrial water + spatial soil heterogeneity | topology/properties derived; dynamic terrestrial water sparse |
| L2 | 64 m | local persistent environmental history | lazy persistent |
| Entity | continuous | people/animals/items/buildings later | future |

The hierarchy remains fixed. Configurability is deferred until a concrete requirement outweighs the persistence and cross-level complexity.

## 2. Unified runtime ownership

The application-level lifecycle boundary remains:

```text
SimulationState
├── World                      authoritative persistent L2 history
├── Continental topology       derived from World
├── WeatherState               authoritative transient atmosphere
└── MultiresolutionWaterState  authoritative conserved water
    ├── terrestrial L0/L1 stores
    └── persistent L0 channel storage
```

`SimulationState` exposes const component views. Its application-level mutation surface is deliberately small:

- one-day coupled advance;
- water refinement;
- water aggregation;
- persistent surface disturbance;
- compound checkpoint save/load.

Lower-level standalone APIs remain available for focused solvers, tests and compatibility, but a normal evolving-world application does not need to coordinate weather/water generations itself.

Continental topology is derived from `World`; it is not another mutable or serialized authority. Channel storage is part of `MultiresolutionWaterState`; it is not another simulation component or checkpoint section.

## 3. World truth vs materialization

The whole world has one truth but does not need one resolution everywhere.

- Static base fields can be reproduced from seed + coordinates.
- Time-dependent state that must exist everywhere is kept cheaply at L0 unless a subsystem requires sparse refinement.
- Persistent or active fine detail exists only where history requires it.
- Querying climate, terrain, weather samples, soil properties or channel volume does not materialize L1/L2 history.
- Materializing terrestrial dynamic water changes conserved ownership and is observable simulation state.
- Channel water remains L0-owned through terrestrial materialize/aggregate transitions.
- Weather is explicit dynamic state but remains L0-only.
- External callers do not receive mutable C++ container access through the unified owner.
- Derived analyses are not promoted to authoritative state if they can be reconstructed from authoritative inputs.

The milestone sequence is:

- v0.3: authoritative whole-world drainage topology;
- v0.4: standalone detailed L1 water;
- v0.5: complete L0 water history + global day;
- v0.6: conservative sparse L1 water ownership;
- v0.7: deterministic spatial soil properties;
- v0.8: capacity-aware water;
- v0.9: authoritative transient weather;
- v0.10: unified runtime ownership + compound checkpoint generation;
- v0.11: persistent conserved channel transport inside multiresolution water.
- v0.12: derived per-reach bounded channel residence from D8 length, filled-elevation slope and accumulated discharge; multiresolution-water persistence v5.

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

Independent random values per 8192 m cell would create checkerboard forcing. Weather innovations are evaluated on a 4×4-L0 synoptic lattice (~32 km at the fixed hierarchy) and interpolated to L0 cells.

Transient anomalies combine previous-day autoregressive persistence, bounded four-neighbor memory and spatially coherent daily innovations.

Storm intermittency uses persistent moisture anomaly plus a coherent daily storm innovation. Long-run regression anchors generated precipitation to static climate. The v0.9 calibration remains unchanged in v0.11.

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

Dynamic conserved terrestrial water cannot exist as two independently advancing copies.

```text
coarse-owned parent:
    L0 terrestrial stores authoritative
    L1 dynamic state absent

refined-owned parent:
    L0 terrestrial stores zero
    L1 8×8 terrestrial stores authoritative
```

Materialization and aggregation conserve snow water equivalent, surface water, soil water and groundwater using actual overlap area.

Soil refinement preserves parent saturation because child capacities differ:

```text
parent_saturation = parent_soil_water / parent_soil_capacity
child_soil_water  = parent_saturation × child_soil_capacity
```

Channel ownership is orthogonal to terrestrial refinement:

```text
every terrestrial L0 parent owns exactly one channel volume
materialize(parent): channel volume unchanged
aggregate(parent):   channel volume unchanged
```

Whole-world water storage is the sum of coarse-owned terrestrial stores, refined-owned terrestrial stores and all L0 channel volumes. Ocean L0 cells must own zero channel water.

## 8. Coordinates and indexing

World positions are double-precision meters. Grid coordinates are signed 64-bit integers with mathematical floor semantics.

World bounds are limited by the floating-point precision required by 64 m L2 cells rather than by the formal int64 range.

Raster indexing rejects extreme out-of-range coordinates without signed-overflowing subtraction. Continental water, refined water, standalone detailed hydrology, weather and channel queries use checked bounds/representability before indexing.

## 9. Hydrology topology

Water topology includes raster and graph semantics:

- per-cell terrain/fill elevation, local yield, accumulated discharge and downstream coordinate;
- river edges;
- lake records with downstream connectivity;
- stable cross-tile ingress/outlet edges for L1 refinement.

The whole-world L0 drainage DAG is the parent ordering/connection truth. A refined parent replaces local terrestrial bucket/routing interior state without replacing the parent L0 downstream relation or its L0 channel ownership.

Authoritative refinement inherits the coarse parent's ocean classification for all active L1 children; the current model does not create mixed land/ocean ownership within a single L0 parent.

## 10. Global time and channel scheduling

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
advance authoritative terrestrial water + channel transport atomically
        ↓
commit next WeatherState
```

A rejected hydrology input cannot advance weather independently.

Channel transport separates start-of-day storage from current-day arrivals. Each terrestrial L0 reach derives a bounded simulation-scale residence:

```text
length_cells = 1 or sqrt(2) from the D8 edge
slope        = max(downhill_gradient, 1e-5)
discharge    = max(accumulated_discharge_m3_s, 1)

residence_days = clamp(
    length_cells
    × (slope / 1e-5)^-0.08
    × (discharge / 100)^-0.06,
    0.75,
    3.0)

release_fraction = 1 - exp(-1 / residence_days)
release = start_of_day_channel_storage × release_fraction
```

Length remains dominant; slope and discharge are deliberately weak modifiers because the daily scheduler does not resolve physical sub-day celerity. The release can cross one L0 downstream edge. Current-day quick runoff/baseflow, upstream arrivals and refined-tile outlet volume are accumulated into next channel storage and cannot contribute to another release during the same day.

For an unrefined downstream parent:

```text
source start-channel release
        ↓ one L0 edge
add to downstream next-channel storage
```

For a refined downstream parent:

```text
source start-channel release
        ↓ one L0 edge
exact deterministic L1 ingress child
        ↓
authoritative 8×8 L1 drainage graph
        ↓
refined external outlet
        ↓
refined parent's next L0 channel storage
```

That refined outlet does not cross the parent's L0 downstream edge until a later global day. Refined current-day runoff follows the same outlet-to-parent-channel rule.

Refined L1 terrestrial water continues to receive its parent L0 atmospheric forcing. No L1 weather downscaling is invented without a separate elevation/orographic/sub-grid contract.

## 11. Forcing boundary and compatibility helpers

Hydrology consumes precipitation, mean air temperature and potential evapotranspiration.

`make_weather_daily_forcing()` exposes authoritative current-day weather through that boundary. Legacy smooth climatological helpers remain available for controlled tests and old focused CLI/API paths.

PET remains a simple temperature-driven approximation because the atmosphere does not yet carry radiation, humidity or wind.

## 12. Determinism

Terrain/climate/soil/topology use deterministic hashing and tie-breaking. Continental routing order is deterministic. Weather innovations are deterministic for world seed + coordinate + integer day.

Channel release uses deterministic start-of-day state and transport derived from deterministic topology fields. New arrivals cannot feed back into release order during the same step, so topological iteration order does not change the one-edge travel-time rule.

The unified checkpoint regressions verify:

- byte-for-byte canonical serialization for identical simulation state on the same build/platform;
- exact weather/coarse/refined reload state;
- exact channel state after reload;
- exact deterministic next-day evolution after reload, including channel state.

Strict bit-identical floating-point results across all compilers/architectures are still not a formal public portability contract.

## 13. Persistence and compound checkpoints

The component formats remain versioned independently.

### World

`World::save()` v2 stores world configuration and materialized L2 history and retains v1 read compatibility. Static terrain/climate/soil remain derived.

### Multiresolution water

Format v5 stores the same authoritative water-state layout introduced by v3: world identity, hydrology parameters, exact day, complete L0 terrestrial state, one channel `double` per L0 cell and sparse refined ownership/state. Local soil capacities, topology and channel transport parameters are re-derived from World identity.

v3 fixed-reservoir and v4 length/slope files preserve their persisted water exactly while adopting current v5 bounded length/slope/discharge transport semantics. Valid v2 files migrate with zero channel storage because v2 had no persistent in-channel authority. Format v1 remains rejected because it predates current spatial soil-capacity semantics.

### Weather

Format v1 stores world identity, process parameters, exact day, raster metadata and per-cell anomaly state. Climate/elevation metadata is reconstructed.

### Simulation checkpoint

The container still has fixed authoritative sections:

```text
World
Weather
Multiresolution Water
```

Channel storage is inside Multiresolution Water; there is no separate channel section. Topology is rebuilt from the World section rather than serialized.

The container stores one global day plus section identifiers, sizes and FNV-1a checksums. On save, the three existing component serializers write private temporary files; a complete same-directory publish file is assembled, revalidated, flushed and atomically renamed/replaced.

The loader validates sizes/checksums, loads World, derives topology, loads weather/water against that identity, then requires the checkpoint global day to match both component clocks before returning `SimulationState`.

FNV-1a is accidental-corruption detection, not cryptographic authentication. Native-POD binary encoding remains the existing format assumption. On POSIX the publish file is fsynced before rename, but the parent directory is not explicitly fsynced afterward; full power-loss directory-entry durability is not claimed.

## 14. Engine boundary

The C ABI remains opaque-handle + POD-copy based so bindings do not depend on C++ ABI/STL layout.

`ws_simulation_state` owns the unified authority and exposes creation/destruction, global-day/ownership counts, regional/weather/water copies, day advance, refinement/aggregation, persistent disturbance and compound checkpoint save/load.

The channel surface exposes read-only per-L0 storage, total channel storage and derived reach transport metadata on both the standalone multiresolution-water handle and the unified simulation handle. Arbitrary channel setters are intentionally absent.

Older standalone World/weather/water C ABIs remain compatible for focused usage.

## 15. Migration and CLI lifecycle

`SimulationState::from_world(World)` remains the migration boundary from pre-v0.10 World saves. It preserves exact World configuration/materialized L2 history and creates aligned day-zero weather/water state. Channel storage starts at zero because legacy World files contain no dynamic channel authority.

CLI lifecycle:

```text
legacy .ws
  └─ simulation-run → compound .wsc
                         └─ simulation-resume → atomically replaced .wsc
```

CTest exercises the `demo → simulation-run → simulation-resume` chain on Linux, sanitizers and MSVC shared-library builds in addition to direct C++/C ABI regressions.

## 16. Scaling

Whole-world weather, terrestrial L0 water and channel water remain compact L0 authorities; L1 terrestrial water exists only for selected refined parents and L2 persistent history remains lazy.

The v0.11 Europe checkpoint CI fixture uses:

- 449,208 L0 cells;
- 64 refined water parents;
- persistent L2 history;
- five warmup unified days before checkpoint;
- non-zero persistent channel storage;
- exact channel equality across every L0 cell after reload;
- exact next-day equivalence after reload, including all channel cells.

One GCC Release CI observation with the bounded residence heuristic measured approximately:

- unified simulation construction: `857.664 ms`;
- materialize 64 parents: `14.077 ms`;
- five unified days: `790.442 ms`;
- checkpoint save: `174.015 ms`;
- checkpoint load including topology reconstruction: `971.509 ms`;
- checkpoint size: `21,769,048 bytes` (`~20.76 MiB`);
- channel storage after five warmup days: `85,711,133,025.076 m³`;
- peak RSS: `266,788 KiB`;
- maximum relative water-balance residual: `5.886e-9`.

These are CI observations, not API/performance guarantees.

## 17. Current strongest limitation

The same-day whole-DAG routing limitation is closed: channel volume is persistent conserved state and only start-of-day storage can release across one L0 edge. Uniform residence time is also closed at the simulation scale: D8 length is the dominant per-reach term, with weak bounded slope/discharge modifiers.

The remaining routing limitation is **temporal/model resolution**, not another missing coefficient. The daily one-L0-edge scheduler cannot represent a flood wave crossing several 8192 m reaches within a day, and the current residence heuristic is intentionally not calibrated against observed gauges.

Further routing work is deferred until a concrete requirement demands one or more of:

- sub-daily/multi-edge propagation;
- empirical travel-time calibration;
- independent hydrograph lag and attenuation;
- channel geometry/capacity or backwater behavior.

At that point the bounded change should alter the routing model/resolution rather than over-calibrate the current heuristic.

Floodplain/wetland exchange, L1 atmospheric downscaling, lateral groundwater, multi-layer soil and geomorphic/vegetation feedback remain separate deferred systems.
