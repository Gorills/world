# Authoritative L0 weather and derived L1 forcing (v0.13)

## Purpose

v0.9 replaces the bundled smooth climatological forcing as the primary evolving atmospheric source with an explicit whole-world weather state.

Static `ClimateSample` remains the reproducible long-run climate baseline. `WeatherState` owns transient day-to-day atmospheric anomalies. Dynamic water still owns only water and continues to consume the same forcing variables it already accepted:

- precipitation depth;
- mean air temperature;
- potential evapotranspiration.

This keeps atmosphere and hydrology as separate state authorities rather than embedding weather inside the water model.

## Resolution and state

Weather is authoritative at L0 climate resolution (8192 m). Every in-world L0 cell owns two transient floats:

```text
temperature_anomaly_c
moisture_anomaly
```

The exact global weather clock is an `int64_t` day.

Repeatedly needed static metadata is cached per L0 cell when the state is constructed:

- actual world-overlap area;
- climate mean temperature corrected for L0 terrain elevation;
- annual precipitation;
- continentality.

That metadata is derived from `WorldConfig`, terrain and climate and is reconstructed on load rather than serialized as a second source of world truth.

Creating or advancing weather does not materialize L1 or L2 persistent state.

## Spatial and temporal structure

Independent random forcing per 8 km cell would produce spatial white noise and would not represent coherent weather systems. v0.9 therefore generates daily innovations on a coarser synoptic lattice:

```text
4 × L0 cells ≈ 32 km
```

Lattice innovations are bilinearly interpolated to L0 cells. The day participates in the deterministic hash, so each day has a new innovation field.

Transient state then evolves with:

- autoregressive memory from the previous day;
- a small blend of the four orthogonal neighboring L0 anomaly states;
- the new spatially coherent synoptic innovation.

The result is deterministic for the same world, parameters and state while retaining both spatial coherence and temporal persistence.

This is synthetic weather scaffolding, not a numerical weather-prediction model.

## Temperature

Daily air temperature is:

```text
terrain-adjusted climate mean
+ deterministic seasonal cycle
+ transient WeatherState temperature anomaly
```

The seasonal amplitude continues to depend on the static continentality field. The transient anomaly is centered around the static climate baseline over long runs.

## Precipitation

The static annual-precipitation field remains the long-run amount target. v0.9 converts that smooth baseline into intermittent wet/dry forcing.

A storm signal combines:

- the persistent moisture anomaly;
- an independent coherent daily storm innovation.

Precipitation is zero below a threshold and grows with storm excess above it. The default storm-intensity multiplier is calibrated by a 10-year regression so the aggregate generated precipitation remains anchored to the static climate baseline rather than silently creating a wetter or drier world.

The original regression fixture currently observes a long-run generated/climatological precipitation ratio of approximately `0.9997`, with a mean wet-area fraction of approximately `0.667`. A second 10-year matrix regression covers multiple seeds and partial/misaligned world bounds so calibration is not accepted from one world identity alone.

These values validate the synthetic default calibration; they are not claims about real-world rain-day frequency.

## Potential evapotranspiration

PET remains deliberately simple:

```text
PET = max(0, 0.10 × (daily_air_temperature_c + 5))
```

v0.9 changes the temperature input from repeating climatology to transient weather. Radiation, humidity and wind are not yet modeled, so PET should still be treated as a forcing approximation rather than a physical Penman-Monteith calculation.

## Weather-to-water coupling

`make_weather_daily_forcing()` exposes the current weather day through the existing `ContinentalWaterForcing` structure without mutating weather.

For authoritative coupled stepping, use:

```cpp
advance_weather_continental_water_day(...)
advance_weather_multiresolution_water_day(...)
```

The weather and water grids must belong to the same `WorldConfig` and their exact integer days must match before the step.

The coupled path prepares the complete current-day forcing and next weather state before water is advanced. Water already advances atomically. Weather is committed only after the water call succeeds. Rejected water input therefore cannot advance the atmospheric clock independently.

`ContinentalWaterState` is the older coarse-only v0.5 C++ API and does not store its construction parameter set internally. Its weather-coupled helper therefore requires an explicit `DynamicHydrologyParameters` argument; callers must pass the same parameter set used to construct the state. The multiresolution state already owns its hydrology parameters, so `advance_weather_multiresolution_water_day()` has no external water-parameter argument.

For a refined L0 water parent, v0.13 derives the 64-slot L1 forcing vector without creating L1 atmospheric state.

For active terrestrial children:

```text
temperature_correction =
    clamp(-0.0065 × (child_effective_elevation_m - parent_effective_elevation_m),
          -8, +8)

child_temperature = parent_temperature + temperature_correction
child_PET         = max(0, 0.10 × (child_temperature + 5))
```

Parent reference elevation uses the actual L0/world overlap center, matching the L0 weather elevation convention. Negative elevations are floored at zero for the lapse correction.

Precipitation uses a weak terrain redistribution:

```text
raw_weight = clamp(
    1 + 0.15 × (child_elevation_m - active_child_mean_elevation_m) / 1000,
    0.75,
    1.25)
```

Raw weights are normalized by actual active-child/world overlap area. After float conversion, the largest-overlap child is adjusted to the closest representable value that minimizes the parent-volume residual. The result conserves the parent precipitation volume to public float-forcing precision, including partial boundary parents.

This is a deterministic simulation-scale terrain heuristic. It is not a windward/leeward precipitation model and it does not introduce a second weather clock or persistence authority.

## Persistence

Weather has a separate explicit persistence file rather than becoming implicit `World::save()` state.

Weather format v1 stores:

- magic/version;
- complete world identity;
- weather-process parameters;
- exact global day;
- L0 raster metadata;
- temperature anomaly and moisture anomaly for every L0 cell.

Static climate, terrain and derived metadata are reconstructed from the supplied `World` on load.

The loader rejects:

- wrong-world identity;
- unsupported format version;
- inconsistent raster metadata;
- non-finite or out-of-range anomaly state;
- truncated files;
- unexpected trailing bytes.

Weather and multiresolution-water retain separate component serializers. Since v0.10, `SimulationState` compound checkpoints coordinate World + Weather + Multiresolution Water as one validated generation. v0.13 adds no L1 weather bytes: refined forcing is reconstructed from authoritative weather, terrain and refined ownership.

## C ABI

`weather_c_api.h` is additive and keeps the existing water C ABI layouts unchanged.

It exposes:

- weather create/destroy;
- exact day and cell count;
- one-cell weather sampling;
- current daily L0 forcing copy;
- read-only derived L1 forcing through the unified simulation C ABI for an already-refined parent;
- standalone weather advance;
- atomic weather + multiresolution-water daily advance;
- weather save/load;
- extension-local error text.

The coupled C entry point operates on the existing opaque multiresolution-water handle rather than duplicating water state.

## Scaling

The Europe-scale CI fixture contains 449,208 L0 cells and 64 refined water parents.

One GCC Release CI observation after default precipitation calibration measured:

- weather-state construction: about 205 ms;
- 30 coupled weather + mixed-resolution water days: about 3.87 s;
- mean coupled day: about 129 ms;
- peak RSS: about 136 MiB;
- maximum relative water-balance residual: `5.9e-9`.

These are runner-specific observations, not timing or memory guarantees. CI enforces correctness/conservation rather than a wall-clock threshold.

## Limitations

v0.9 weather does not yet model:

- pressure or explicit wind fields;
- humidity/dew point;
- radiation/cloud physics;
- fronts, cyclogenesis or numerical atmospheric dynamics;
- data assimilation;
- persistent L1 or L2 atmospheric state;
- physical windward/leeward orographic precipitation;
- climate change or long-term climate drift.

Static climate is still synthetic. Weather is a coherent stochastic transient layer around that synthetic baseline.
