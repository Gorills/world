# Sparse persistent vegetation — v0.14

## Purpose

v0.14 adds the first persistent ecology-facing state without creating a whole-world ecology raster.

Static `forest_potential` already existed as deterministic local terrain/climate carrying potential. v0.14 adds one dynamic normalized `vegetation_biomass` value to each materialized 64 m L2 cell and makes the existing persistent `disturbance` field affect and recover that biomass.

Vegetation is part of existing `World` local history. There is no independent `VegetationState`, vegetation clock or fourth compound-checkpoint section.

## Sparse ownership

```text
unmaterialized regional cell
    deterministic terrain / forest potential only
    no persistent L2 vegetation allocation

materialized regional cell
    16 × 16 local cells at 64 m
    persistent disturbance
    persistent vegetation_biomass
```

Only already-materialized local patches advance during a simulation day. Vegetation queries on unified `SimulationState` do not materialize missing patches.

Standalone `World` copy helpers retain their older materializing behavior where documented.

## State variables

Each persistent L2 cell has:

```text
forest_potential      derived static 0..1 carrying-potential proxy
disturbance           persistent 0..1 surface disturbance
vegetation_biomass    persistent 0..1 live-cover/biomass proxy
```

The invariant is:

```text
0 <= vegetation_biomass
   <= forest_potential * (1 - disturbance)
   <= 1
```

Below sea level or outside actual world overlap, persistent biomass is zero.

Newly materialized in-world land starts with:

```text
disturbance = 0
vegetation_biomass = forest_potential
```

## Immediate disturbance

`disturb_surface()` retains its existing monotonic disturbance command: a call only changes a cell when the requested amount exceeds stored disturbance.

When disturbance increases:

```text
disturbance = requested_amount

vegetation_biomass =
    min(vegetation_biomass,
        forest_potential * (1 - disturbance))
```

Thus clearing/disturbance has an immediate persistent vegetation effect rather than waiting for the next daily step.

## Daily recovery

Disturbance recovers independently with a 730-day e-folding time:

```text
disturbance_next = disturbance * exp(-1 / 730)
```

The post-decay biomass target is:

```text
target = forest_potential * (1 - disturbance_next)
```

Current-day environmental suitability is:

```text
temperature_factor = clamp((mean_air_temperature_c + 2) / 18, 0, 1)
moisture_factor    = clamp((soil_saturation - 0.10) / 0.60, 0, 1)
suitability        = temperature_factor * moisture_factor
```

Biomass recovery uses a 365-day base e-folding time:

```text
recovery_fraction = 1 - exp(-suitability / 365)

vegetation_biomass_next =
    vegetation_biomass
    + (target - vegetation_biomass) * recovery_fraction
```

Cold or dry conditions can therefore stall biomass recovery while disturbance continues to decay.

These time constants and response curves are simulation-scale heuristics, not calibrated ecological growth rates.

## Environmental forcing

`make_materialized_vegetation_forcing()` derives exactly one record for every currently materialized regional patch.

Temperature comes from the current L0 `WeatherState` record for that regional patch's climate parent.

Soil saturation uses the authoritative water owner:

- if the L0 parent is refined, the exact corresponding L1 regional soil-water cell and local L1 soil capacity;
- otherwise, the parent L0 soil-water state and coarse soil capacity.

The function is read-only and does not materialize World or water state.

## Unified daily atomicity

`SimulationState::advance_day_full()` closes one generation as:

```text
validate current component identity/clocks
        ↓
derive vegetation forcing from current weather/water
        ↓
copy sparse World and advance vegetation in staged history
        ↓
advance weather + authoritative water atomically
        ↓
no-throw swap of staged local World history
        ↓
validate unified invariants
```

If vegetation forcing/staging fails, weather and water have not advanced.

If weather/water rejects, the real World local history has not changed.

The older `SimulationState::advance_day()` signature remains source-compatible. It delegates to the full step and returns only the historical weather/water report.

Vegetation has no independent integer clock: its generation is the enclosing simulation day whenever it is advanced through `SimulationState`.

## C++ API

The focused standalone surface is:

```cpp
world.materialized_patch_coords();
world.advance_materialized_vegetation_day(forcing);
make_materialized_vegetation_forcing(world, weather, water);
```

Unified stepping additionally exposes:

```cpp
SimulationDayReport report = simulation.advance_day_full();
```

where `report.environment` is the existing weather/water report and `report.vegetation` contains aggregate sparse-vegetation diagnostics.

## C ABI

Existing C PODs and signatures are not extended or reordered.

New additive PODs/functions include:

- `ws_local_vegetation_cell`;
- `ws_vegetation_forcing`;
- `ws_vegetation_step_report`;
- `ws_world_copy_local_vegetation()`;
- `ws_world_advance_materialized_vegetation_day()`;
- `ws_simulation_copy_local_vegetation()`;
- `ws_simulation_advance_day_v2()`.

The unified local-vegetation copy requires existing materialized history and does not create it.

The old `ws_simulation_advance_day()` remains unchanged and still advances vegetation because it delegates to the same unified simulation generation.

## Persistence

World persistence is format **v3**.

v3 appends one `float vegetation_biomass` to every serialized materialized L2 cell. The World component remains the sole persistence authority for sparse local history.

v1/v2 migration preserves all fields that existed in those formats. Missing biomass is reconstructed deterministically:

```text
if local cell overlaps world and elevation > sea_level:
    biomass = forest_potential * (1 - disturbance)
else:
    biomass = 0
```

The loader validates finiteness, normalized ranges, disturbed carrying-capacity bounds, sea-level constraints, patch bounds, duplicate coordinates, truncation and trailing data.

Compound `SimulationState` checkpoints still contain exactly:

```text
World
Weather
Multiresolution Water
```

Vegetation is inside the World section; no new checkpoint section or duplicate authority exists.

## Regression coverage

Permanent tests cover:

- initial biomass from local forest potential;
- immediate biomass suppression by disturbance;
- warm/moist recovery;
- cold/dry stalled biomass with continuing disturbance decay;
- invalid/missing forcing atomicity;
- no accidental L2 materialization from vegetation advance/query paths;
- World v3 exact/canonical round trip;
- deterministic World v2 → v3 biomass migration;
- unified environment + vegetation day ownership;
- exact compound checkpoint vegetation reload;
- exact deterministic vegetation future after checkpoint reload;
- standalone and unified C ABI behavior.

## Europe-scale gate

The permanent unified checkpoint benchmark exercises:

- 449,208 L0 cells;
- 64 refined water parents;
- 64 materialized vegetation patches;
- 16,384 disturbed L2 cells;
- five unified warmup days;
- exact checkpoint reload of all vegetation patches;
- exact next-day environment + vegetation future equivalence.

One GCC Release CI observation measured:

- five unified days: about `739.681 ms`;
- checkpoint save: about `386.885 ms`;
- checkpoint load: about `751.586 ms`;
- checkpoint size: `22,093,640 bytes`;
- peak RSS: `270,440 KiB`;
- maximum relative water-balance residual: `5.886e-9`.

These are runner-specific observations rather than public performance guarantees.

## Deliberate limitations

v0.14 does not model:

- plant species or functional types;
- age/size structure;
- succession or inter-species competition;
- seed dispersal or migration;
- fire;
- nutrients;
- carbon pools or carbon flux;
- grazing;
- crop management;
- vegetation feedback into infiltration, ET, erosion or channel behavior.

Those features should be introduced only when a gameplay/entity consumer needs them.
