# v0.8 architecture audit

## Scope audited

v0.8 connected the deterministic v0.7 soil-property field to the authoritative water buckets.

The audit checked the boundaries that matter for selecting the next subsystem rather than expanding the soil model automatically:

- L0/L1 water ownership;
- conservative materialize/aggregate behavior under heterogeneous soil capacity;
- persistence semantics;
- forcing ownership;
- global time;
- Europe-scale cost.

## v0.8 result

The capacity-aware soil integration is a stable boundary for the next step.

Verified properties from the merged implementation and CI:

- storage/field/wilting thresholds and initial soil water use the same local storage scale;
- infiltration uses the independent local infiltration scale;
- L0→L1 soil transfer preserves parent saturation and total volume under the v0.7 area-weighted capacity invariant;
- L1→L0 aggregation remains volume based and validates the parent-equivalent capacity;
- refined parents do not own an independent L0 water copy;
- mixed-resolution daily water balance remains within the existing floating-point tolerance;
- existing water C POD layouts remain unchanged;
- `World::save()` remains independent of dynamic water and derived soil properties;
- multiresolution-water persistence v2 explicitly represents the new local-capacity semantics.

The final v0.8 PR and post-merge main CI passed GCC, Clang and ASan/UBSan. The Europe-scale fixture remained within the existing conservation gate.

## Strongest remaining dependency

After v0.8, two large limitations were immediate candidates:

1. atmospheric forcing was still a deterministic smooth climatological helper;
2. channel routing still moved daily runoff/baseflow through the drainage DAG without travel-time/flood-wave state.

Both are material. Weather is the correct next dependency.

### Why weather first

The water model already exposes a stable forcing contract:

```text
precipitation
mean air temperature
potential evapotranspiration
```

Hydrology explicitly does not own weather. Replacing the smooth helper with an authoritative atmospheric subsystem can therefore be done without changing:

- conserved water stores;
- L0/L1 water ownership;
- drainage topology;
- soil-capacity semantics;
- multiresolution-water persistence layout.

This gives a large behavioral improvement while preserving the state boundaries that v0.5-v0.8 progressively hardened.

### Why channel travel time is deferred one slice

Adding travel time is not merely a new diagnostic. Water that currently traverses the routing DAG within one daily step would need persistent in-channel or delayed-routing storage.

That immediately raises new conservation/ownership questions:

- where channel water is stored between days;
- how L0 channel storage transfers when a parent is refined;
- how L1 outlet storage aggregates back to L0;
- whether channel state belongs to cells or graph edges;
- how persistence formats change;
- how partial refinement interacts with travel-time state.

Those are valid next problems, but combining them with a new WeatherSystem would make failure attribution and persistence migration unnecessarily broad.

## Selected v0.9 slice

v0.9 therefore introduces an authoritative whole-world L0 WeatherState with:

- one exact integer global day;
- compact transient temperature/moisture anomalies;
- spatially coherent daily synoptic innovations;
- temporal and neighbor memory;
- intermittent precipitation anchored to the static climate baseline;
- transient temperature and PET forcing;
- direct use of the existing water forcing boundary;
- atomic weather/water clock coupling;
- separate versioned persistence;
- additive C ABI;
- long-run climatology and Europe-scale coupled regressions.

This is intentionally a full subsystem rather than a change to the old smooth helper.

## Adversarial checks applied to v0.9 design

### Independent cell noise

Failure mode: a random value per 8 km cell would create checkerboard weather and unrealistic forcing gradients.

Resolution: innovations are generated on a coarser synoptic lattice and interpolated spatially; regression compares neighboring and distant anomaly differences.

### Climate-baseline drift

Failure mode: adding storm intermittency can preserve wet/dry behavior while silently changing long-run annual precipitation.

This failure was actually detected during the v0.9 implementation. The initial default storm multiplier produced only about `0.646` of the static climate precipitation over the 10-year regression fixture.

Resolution: wet-area frequency was already acceptable, so only the linear storm-intensity multiplier was calibrated. The corrected default produces approximately `0.9997` of the static climate precipitation on the regression fixture without changing the wet/dry frequency.

### Weather/water clock divergence

Failure mode: weather could advance even when the hydrology step rejects its input.

Resolution: coupled stepping prepares the next weather state before water runs and commits it only after the existing atomic water step succeeds. Exact clock equality is a precondition.

### Eager fine allocation

Failure mode: introducing weather could accidentally allocate L1/L2 atmosphere across Europe.

Resolution: v0.9 owns only an L0 atmospheric state; construction and 365-day regressions verify no L2 materialization.

## Deferred after v0.9

The strongest hydrologic architecture limitation after weather is expected to be persistent channel travel-time/routing state, but that should be re-audited against actual v0.9 behavior before implementation.

Also deferred:

- L1 atmospheric downscaling;
- physical humidity/radiation/wind;
- lateral groundwater;
- multi-layer soil;
- vegetation and erosion feedback.
