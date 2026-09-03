# Architecture decisions — v0.9

## 1. Resolution hierarchy

| Level | Resolution | Purpose | Storage |
|---|---:|---|---|
| L0 | 8192 m | static climate baseline + transient weather + authoritative continental drainage + default dynamic water history + parent-equivalent soil properties | whole-world derived topology/properties + compact weather/water state |
| L1 | 1024 m | regional terrain, authoritative drainage refinement + selectively authoritative detailed water + spatial soil heterogeneity | topology/properties derived; dynamic water sparse |
| L2 | 64 m | local persistent environmental history | lazy persistent |
| Entity | continuous | people/animals/items/buildings later | future |

The hierarchy remains fixed. Configurability is deferred until there is a concrete requirement that outweighs the extra persistence and cross-level complexity.

## 2. World truth vs materialization

The whole world has one truth but does not need one resolution everywhere.

- Static base fields can be reproduced from seed + coordinates.
- Time-dependent state that must exist everywhere is kept cheaply at L0 unless a concrete subsystem requires sparse refinement.
- Persistent or active fine detail exists only where history requires it.
- Querying climate, terrain or soil properties does not materialize L1/L2 state.
- Materializing dynamic water changes ownership and is therefore observable simulation state.
- Weather is explicit dynamic state but remains L0-only in v0.9; it does not materialize L1/L2 atmosphere.
- External callers do not receive mutable access to authoritative C++ containers; mutations go through commands.
- Derived analyses are not promoted to authoritative truth if they depend on arbitrary query boundaries.

v0.3 established authoritative continental topology. v0.4 added standalone detailed L1 water. v0.5 added complete L0 water history and one global day. v0.6 made sparse L1 tiles true refinements of that evolving history. v0.7 added deterministic static L0/L1 soil properties. v0.8 made those properties part of the bucket model. v0.9 adds the first independent whole-world transient atmospheric state while keeping the established water ownership boundary intact.

## 3. Static climate vs transient weather

Static `ClimateSample` remains reproducible long-run world truth. It provides:

- mean temperature baseline;
- annual precipitation baseline;
- continentality.

`WeatherState` owns transient daily atmospheric anomalies instead of changing the climate field itself.

Each L0 weather cell stores only:

```text
temperature_anomaly_c
moisture_anomaly
```

Climate/elevation metadata used repeatedly by the daily model is cached when `WeatherState` is constructed, but it is derived and rebuilt on persistence load.

Daily forcing is obtained by combining:

```text
static climate
+ deterministic seasonal cycle
+ transient weather anomalies
+ coherent storm intermittency
```

The resulting precipitation, temperature and PET values use the same forcing structures the water model already consumed before v0.9.

Weather therefore owns atmosphere and water owns water; neither embeds the other's conserved state.

## 4. Weather spatial/temporal coherence

Independent random values at every 8192 m cell would create checkerboard forcing. v0.9 instead evaluates deterministic daily innovations on a 4×4-L0 synoptic lattice (approximately 32 km at the fixed hierarchy) and interpolates them to L0 cells.

Transient anomalies then combine:

- autoregressive persistence from the previous day;
- a bounded contribution from the four orthogonal neighboring L0 states;
- the new spatially coherent daily innovation.

This provides bounded spatial and temporal coherence without storing a second fine atmospheric raster.

Storm intermittency uses the persistent moisture anomaly plus an independent coherent daily storm innovation. A 10-year regression guards the stronger climate invariant: intermittency must not silently change the long-run static annual-precipitation baseline. The initial implementation failed this check at roughly 0.646 of climatology; the default linear storm-intensity scale was calibrated so the regression fixture is approximately 0.9997 while wet-area frequency remains unchanged.

This is synthetic stochastic weather scaffolding, not atmospheric dynamics or numerical weather prediction.

## 5. Soil property truth and effective bucket parameters

`SoilProperties` contains two positive dimensionless modifiers:

- storage-capacity scale;
- infiltration-capacity scale.

The hydrology parameters remain configurable reference values. Effective local values are derived as:

```text
soil_capacity         = reference_soil_capacity         × storage_scale
field_capacity        = reference_field_capacity        × storage_scale
wilting_point         = reference_wilting_point         × storage_scale
initial_soil_water    = reference_initial_soil_water    × storage_scale
infiltration_capacity = reference_infiltration_capacity × infiltration_scale
```

Scaling the three storage thresholds and initial water together preserves their relative bucket geometry and configured initial saturation.

The parent L0 property is directly reproducible from world seed + climate coordinate. L1 children have deterministic heterogeneity but are normalized by actual world-overlap area so their area-weighted mean is the direct parent value:

```text
parent_scale = Σ(child_scale × child_overlap_area)
               --------------------------------------
                    Σ(child_overlap_area)
```

This also holds for partial parents at configured world boundaries. Soil properties are derived static world truth and are not serialized.

## 6. Water ownership and coarse/fine conservation

Dynamic conserved water fields cannot exist as two independently advancing copies.

```text
coarse-owned:
    L0 parent stores authoritative
    L1 dynamic state absent

refined-owned:
    L0 parent stores zero
    L1 8×8 stores authoritative
```

Materialization and aggregation conserve separately:

- snow water equivalent;
- surface water;
- soil water;
- groundwater.

Transfer uses actual world-overlap area. Soil uses saturation-preserving refinement because child capacities differ:

```text
parent_saturation = parent_soil_water / parent_soil_capacity
child_soil_water  = parent_saturation × child_soil_capacity
```

The v0.7 parent-equivalent capacity invariant makes this volume-conservative by construction. Snow, surface water and groundwater continue to transfer by parent depth because no spatial capacity field currently applies to those stores.

Weather does not participate in this ownership transfer. A refined L1 water tile receives the current atmospheric forcing of its L0 weather parent.

## 7. Coordinates

World positions are double-precision meters. Grid coordinates are signed 64-bit integers and use mathematical floor semantics, including negative positions.

Configured world bounds are limited by floating-point precision required by 64 m L2 cells rather than by the much larger formal int64 range.

Raster indexing must reject extreme out-of-range coordinates without signed arithmetic overflow. Continental water, multiresolution refined water, standalone detailed hydrology and weather use checked lower bounds/representability and unsigned non-negative deltas where required.

## 8. Hydrology representation

Water topology is not represented only as a raster.

The L0 whole-world result owns basin/outlet topology. Fixed 8×8 L1 tiles refine that topology. Hydrology contains raster state plus graph semantics:

- per-cell terrain/fill elevation, local yield, accumulated discharge and downstream coordinate;
- graph-like river edges;
- explicit lake records with downstream connectivity;
- stable cross-tile ingress/outlet edges for authoritative L1 refinement.

The mixed scheduler uses the immutable L0 drainage DAG as the parent ordering/connection truth. A refined parent replaces the local L0 bucket/routing interior, not the continental downstream relation.

Authoritative refinement inherits the coarse parent's ocean classification for every active L1 child. The current ownership boundary therefore never creates a mixed land/ocean child mask inside one L0 parent.

## 9. Global time and weather/water scheduling

`WeatherState`, `ContinentalWaterState` and `MultiresolutionWaterState` use exact signed 64-bit integer days.

Within multiresolution water, every sparse refined tile records the same day as the coarse owner. A mismatch is invalid.

For weather-driven coupled stepping, weather and water must also start on the same day and the same L0 grid/world identity.

```text
prepare current-day weather forcing
prepare next WeatherState in scratch storage
        ↓
advance authoritative water atomically
        ↓
commit next WeatherState
```

This preserves the stronger clock invariant:

```text
weather.day == water.day
```

A hydrology rejection cannot advance weather independently.

Inside the water step, the existing L0 topological order remains unchanged:

```text
unrefined parent
    → capacity-aware coarse bucket step
    → coarse route

refined parent
    ← all upstream channel ingress already collected
    → capacity-aware detailed 8×8 L1 step using parent L0 weather forcing
    → one external outlet volume
    → parent L0 downstream relation
```

v0.9 deliberately does not invent L1 atmospheric downscaling before a separate elevation/orographic/sub-grid forcing contract exists.

## 10. Forcing boundary and legacy helpers

Hydrology consumes:

- precipitation;
- mean air temperature;
- potential evapotranspiration.

That boundary predates WeatherSystem and remains the integration seam.

`make_weather_daily_forcing()` exposes authoritative current-day weather through that boundary.

The old smooth climatological helpers remain available for deterministic controlled tests and older CLI paths. They are no longer the only evolving-world atmospheric source.

PET is still a deliberately simple temperature-driven approximation because weather does not yet carry radiation, humidity or wind.

## 11. Determinism

Static terrain/climate/soil state and hydrology use deterministic hashing and explicit tie-breaking. Continental routing uses a deterministic topological order. Tile boundary connections use deterministic elevation/tie-break rules.

Weather innovation fields are deterministic for the same world seed, coordinates and integer day. Persistent anomaly state plus deterministic neighbor calculations therefore gives repeatable evolution from identical state and parameters.

Weather persistence regression verifies that a reloaded state produces exactly the same next forcing as the original state.

Strict bit-identical cross-platform floating-point determinism remains a future contract decision.

## 12. Persistence

Three persistence authorities remain distinct.

### World persistence

`World::save()` format v2 stores world configuration and persistent materialized L2 patches and retains v1 read compatibility.

Static terrain/climate/soil properties are derived and are not serialized as rasters.

### Dynamic-water persistence

Multiresolution-water format v2 stores:

- world identity;
- reference hydrology parameters;
- exact global day;
- complete L0 dynamic state;
- sparse refined parent ownership and child states.

Effective soil capacities are re-derived from world identity.

### Weather persistence

Weather format v1 stores:

- world identity;
- weather-process parameters;
- exact global day;
- L0 raster metadata;
- per-cell temperature and moisture anomalies.

Climate/elevation metadata is reconstructed from `World` on load.

Weather and water remain separate files. v0.9 does not introduce a compound transactional disk checkpoint. Applications needing one must coordinate the two explicit state saves externally.

## 13. Engine boundary

The base C ABI continues to use opaque handles plus POD copy functions so engine bindings do not depend on C++ ABI/STL containers.

Existing extensions remain:

- `ws_multiresolution_water_state` for dynamic-water ownership;
- `soil_c_api.h` for derived soil queries.

v0.9 adds an opaque `ws_weather_state` and additive weather POD reports/samples. It can:

- query/simulate/persist weather independently;
- copy current forcing;
- advance weather and an existing multiresolution-water handle in one atomic coupled call.

Existing water POD layouts and signatures do not change.

Game engines render/query the simulation and submit commands; they do not own authoritative state.

## 14. Scaling

The complete world now keeps compact L0 dynamic state for both water and weather.

Per weather L0 cell, persistent transient state is two floats. Derived metadata caches overlap area and climate/elevation values for repeated daily use. No all-Europe L1/L2 atmospheric raster is allocated.

Only selected water parents allocate 64 detailed L1 water cells plus their fixed authoritative tile topology.

The calibrated v0.9 Europe CI fixture uses 449,208 L0 cells and 64 refined water parents. One GCC Release observation measured approximately:

- weather-state creation: 205 ms;
- 30 coupled weather + water days: 3.87 s;
- mean coupled day: 129 ms;
- peak RSS: 136 MiB;
- maximum relative water-balance residual: `5.9e-9`.

Measured timings/RSS are environment-specific observations, not API guarantees.

## 15. Current strongest limitation

The forcing boundary now has transient spatially coherent weather instead of only repeating climatology. The strongest remaining hydrologic architecture limitation is likely the same-day channel routing assumption: quickflow/baseflow can traverse the L0 drainage DAG within one daily step and no persistent in-channel/travel-time state exists.

That next problem must be treated as conserved dynamic state, including ownership across L0/L1 refinement and persistence, rather than as a cosmetic delay parameter.

Other material deferred areas include L1 atmospheric downscaling, humidity/radiation/wind, lateral groundwater and the single-layer soil bucket. A post-v0.9 audit should confirm priority before implementation.
