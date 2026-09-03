# Continental dynamic water state (v0.5 coarse foundation)

> Historical v0.5 design note. The current v0.11 architecture adds authoritative `WeatherState`, multiresolution terrestrial ownership, spatial soil capacity and persistent L0 channel transport on top of this coarse-water foundation. The smooth forcing helper and same-day coarse routing description below remain legacy standalone behavior, not the current `MultiresolutionWaterState` scheduler. See `docs/CHANNEL_TRANSPORT.md` for current channel semantics.

## Purpose

v0.4 can simulate detailed water stores on one authoritative 8×8 L1 tile, but a tile created on day 500 previously started from fixed initial bucket values. That violates persistent-world history. v0.5 introduces a cheap authoritative dynamic state at L0 for the complete configured world so every location has a water history before detailed materialization.

## Ownership

`ContinentalWaterState` owns:

- originating `WorldConfig` identity;
- exact `int64_t simulated_day`;
- one compact `ContinentalWaterCellState` per L0 topology cell;
- immutable cached cell metadata needed by the solver;
- immutable topological routing order derived from the authoritative continental drainage graph.

Callers can read cell state but cannot mutate the clock, stores, routing metadata or ordering directly.

In current multiresolution use, `ContinentalWaterState` is embedded as the coarse terrestrial part of `MultiresolutionWaterState`; persistent channel volume is owned separately by that enclosing state rather than added to this legacy cell POD.

## Daily forcing boundary

`ContinentalWaterForcing` is external input:

```text
precipitation depth for the day
mean air temperature
potential evapotranspiration depth
```

`make_smooth_continental_daily_forcing()` is deterministic climate-only scaffolding, not the weather model. Since v0.9, authoritative `WeatherState` supplies the same records through this existing boundary.

Forcing for the entire state is validated before mutation. Invalid size, negative/non-finite precipitation/PET or non-finite temperature rejects the step atomically. Ocean atmospheric forcing is valid; terrestrial hydrology simply excludes ocean cells from stores and water-balance accounting.

## Local bucket processes

For each terrestrial L0 cell, one daily step performs:

1. rain/snow partition from mean temperature;
2. degree-day snowmelt;
3. surface-water accumulation;
4. saturation-dependent infiltration bounded by soil capacity;
5. surface evaporation and moisture-stressed soil ET;
6. percolation above field capacity;
7. groundwater recession/baseflow;
8. quick runoff above surface-storage capacity.

The v0.5 parameterization is the coarse foundation used by later capacity-aware behavior.

## Legacy standalone routing

The standalone `advance_continental_water_day()` path converts local quick runoff + baseflow from depth to volume using the actual in-world overlap area of the L0 cell and traverses volumes in the immutable topological order of the drainage DAG.

Upstream volume contributes to downstream routed discharge but is not re-infiltrated into downstream soil during the same coarse step. Terminal nodes contribute to `terminal_outflow_m3`.

This paragraph describes the legacy standalone coarse solver. Since v0.11, authoritative mixed-resolution simulation uses persistent L0 channel storage: only start-of-day channel water can release and cross at most one L0 edge per global day. Current-day runoff first enters channel storage instead of traversing the whole L0 DAG immediately.

## Conservation

The report computes:

```text
error = storage_before
      + terrestrial_precipitation
      - terrestrial_evapotranspiration
      - terminal_outflow
      - storage_after
```

For the standalone coarse solver, storage is the four terrestrial bucket stores. For current multiresolution simulation, the enclosing scheduler also includes persistent L0 channel storage in `storage_before`/`storage_after`.

State depths are floats while balance accumulation is double, so small residuals from float store updates are expected and tested with an appropriate relative/absolute tolerance.

## Scaling

The Europe fixture used in the project has 449,208 L0 cells. The v0.4 audit showed that eagerly storing detailed L1 water state would require about 1.50 GiB for water cells alone. The coarse foundation stores one compact terrestrial cell per L0 cell and caches only metadata required for daily stepping; v0.11 adds one `double` channel volume per L0 cell in the enclosing multiresolution owner.

Timing/RSS observations are environment-specific measurements, not API guarantees.

## C ABI

The standalone coarse C ABI exposes an opaque `ws_continental_water_state` and functions to:

- create/destroy state;
- read cell count/global day;
- copy cells;
- produce temporary smooth daily forcing;
- advance exactly one legacy coarse day and obtain the conservation report.

The opaque handle stores the construction parameters so a C caller cannot accidentally advance the same state with a different parameter set.

Current channel queries live on `ws_multiresolution_water_state` and `ws_simulation_state`; the old `ws_continental_water_cell_state` layout is unchanged.

## Historical deferred list

The following items were deferred by v0.5. Several were implemented in later milestones:

- persistence of continental dynamic state;
- L0→L1 conservative disaggregation;
- L1→L0 aggregation;
- detailed-tile activation scheduling;
- real weather;
- spatial soil properties;
- persistent channel travel time;
- floodplain/wetland exchange;
- lateral groundwater;
- erosion/sediment;
- vegetation feedback.

Persistent channel travel time is implemented in v0.11. Current status and remaining limitations are documented in `docs/ARCHITECTURE.md`, `docs/CHANNEL_TRANSPORT.md` and `docs/AUDIT_v0.11.md`.
