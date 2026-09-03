# Continental dynamic water state (v0.5)

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

## Daily forcing boundary

`ContinentalWaterForcing` is external input:

```text
precipitation depth for the day
mean air temperature
potential evapotranspiration depth
```

`make_smooth_continental_daily_forcing()` is temporary deterministic scaffolding, not the weather model. A future WeatherSystem can supply the same records.

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

The parameters remain generic scaffolding until spatial soil types exist.

## Routing

Local quick runoff + baseflow is converted from depth to volume using the actual in-world overlap area of the L0 cell. Volumes are traversed in the immutable topological order of the authoritative continental drainage DAG.

Upstream volume contributes to downstream routed discharge but is not re-infiltrated into downstream soil during the same coarse step. The model therefore represents channel routing at this level, not floodplain exchange.

Terminal nodes contribute to `terminal_outflow_m3`.

## Conservation

The report computes:

```text
error = storage_before
      + terrestrial_precipitation
      - terrestrial_evapotranspiration
      - terminal_outflow
      - storage_after
```

State depths are floats while balance accumulation is double, so small residuals from float store updates are expected and tested with an absolute tolerance appropriate to the fixture scale.

## Scaling

The Europe fixture used in the project has 449,208 L0 cells. The v0.4 audit showed that eagerly storing detailed L1 water state would require about 1.50 GiB for water cells alone. v0.5 stores one compact coarse cell per L0 cell and caches only metadata required for daily stepping.

A local Release/GCC benchmark on the fixture observed roughly 85 MB peak RSS for world/topology/state/forcing and about 0.025 seconds per simulated coarse day after setup. These are environment-specific measurements, not API guarantees.

## C ABI

The C ABI exposes an opaque `ws_continental_water_state` and functions to:

- create/destroy state;
- read cell count/global day;
- copy cells;
- produce temporary smooth daily forcing;
- advance exactly one day and obtain the conservation report.

The opaque handle stores the construction parameters so a C caller cannot accidentally advance the same state with a different parameter set.

## Explicitly deferred

- persistence of continental dynamic state;
- L0→L1 conservative disaggregation;
- L1→L0 aggregation;
- detailed-tile activation scheduling;
- real weather;
- spatial soil properties;
- channel travel time/hydraulics;
- floodplain/wetland exchange;
- lateral groundwater;
- erosion/sediment;
- vegetation feedback.

The next layer is conservative L0↔L1 state transfer. Without it, a detailed tile still cannot inherit its parent's accumulated water history correctly.
