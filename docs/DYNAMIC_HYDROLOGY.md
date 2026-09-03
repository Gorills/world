# Dynamic hydrology — v0.4

## Scope

v0.4 adds a time-dependent water-cycle state on top of the authoritative L0→L1 drainage topology introduced in v0.3.

The dynamic solver operates on one fixed authoritative 8×8 L1 tile (64 regional cells). It does **not** invent drainage directions: every routed volume follows the tile topology produced by `refine_authoritative_hydrology_tile()`.

This is intentionally a compact bucket model. It is designed to establish conserved state and stable contracts before soil types, vegetation feedback, radiation, humidity, channel hydraulics, erosion, or a full weather system exist.

## State per active L1 cell

Stored water depths are in millimetres of water equivalent:

- `snow_water_equivalent_mm`
- `surface_water_mm`
- `soil_water_mm`
- `groundwater_mm`

The state also stores diagnostics from the most recent advance call:

- evapotranspiration;
- quick runoff;
- baseflow;
- average routed discharge.

`DynamicHydrologyTileState::simulated_days` records how much time that state instance has advanced.

## Forcing contract

Hydrology consumes external hydrometeorological forcing per L1 cell:

- total precipitation depth over the requested step;
- mean air temperature;
- potential evapotranspiration depth over the requested step.

This separation is deliberate. A later weather/atmosphere system can replace the forcing provider without rewriting the hydrology state transition.

v0.4 includes `make_smooth_climatological_forcing()` only as a deterministic pre-weather provider for tests, CLI runs, and integration work. It is **not** presented as realistic weather.

The helper uses:

- L0 annual precipitation distributed by a smooth seasonal weight whose annual mean is 1;
- a sinusoidal temperature cycle with amplitude influenced by `continentality`;
- the existing 6.5 K/km elevation lapse-rate approximation;
- a simple temperature-derived PET proxy.

## Water-cycle transition

For each internal substep (`<= 1 day`):

1. Precipitation is partitioned linearly from all-snow at `-1 °C` to all-rain at `+1 °C`.
2. Snowmelt uses a degree-day coefficient and moves water from snow storage to surface storage.
3. Surface water infiltrates into the soil bucket subject to:
   - remaining soil capacity;
   - an infiltration capacity that decreases as the soil approaches saturation.
4. Potential evapotranspiration removes water from:
   - surface storage first (limited share);
   - then soil storage, reduced by a moisture-stress factor between wilting point and field capacity.
5. Soil water above field capacity percolates to groundwater with an exponential recession fraction.
6. Groundwater generates baseflow with an exponential recession fraction.
7. Surface water above the configured surface-storage capacity becomes quick runoff.
8. Quick runoff + baseflow + external channel inflow are routed through the authoritative L1 drainage graph.
9. A route that reaches an external tile edge or terminal world-boundary outlet is counted as `external_outflow_m3`.

Snowmelt, infiltration, percolation and baseflow are internal transfers until water enters routed channel flow; they do not create or destroy water.

## Conservation invariant

Every advance reports:

```text
storage_before
+ precipitation
+ external_inflow
- evapotranspiration
- external_outflow
- storage_after
= water_balance_error
```

The test suite checks this daily and across specific snow/thaw and channel-routing fixtures.

State depth is currently stored as `float`, while accounting is accumulated in `double`. Therefore the expected error is small floating-point state-rounding noise, not exact zero.

## Cross-tile coupling

A dynamic tile accepts `ExternalHydrologyInflow { coord, volume_m3 }` records.

The intended coupling is:

```text
upstream tile external outlet
        ↓ volume over step
known downstream L1 ingress coordinate
        ↓
downstream tile external inflow
```

v0.4 provides this volume/coordinate contract but does not yet provide a world scheduler that orders and advances all active tiles automatically.

## Identity and safety

`AuthoritativeHydrologyTile` now carries the `WorldConfig` identity that produced it. Dynamic tile state carries the same identity.

The solver rejects using a tile/state with a different `World`, preventing a valid-looking call from silently using the wrong bounds, sea level, seed, or hierarchy.

## Current defaults

The default bucket parameters are model parameters, not measured Europe-wide constants:

| Parameter | Default |
|---|---:|
| soil capacity | 260 mm |
| field capacity | 160 mm |
| wilting point | 45 mm |
| infiltration capacity | 24 mm/day |
| surface storage capacity | 8 mm |
| percolation rate | 0.08/day |
| groundwater recession | 0.015/day |
| snow melt coefficient | 3 mm/(°C·day) |
| initial soil water | 120 mm |
| initial groundwater | 40 mm |

They exist so the state machine is explicit and testable before a soil/geology layer supplies spatially varying properties.

## Known limitations

- Dynamic state is currently an explicit simulation object/handle, not part of `World` save files yet.
- There is no global simulation clock or lazy catch-up scheduler yet.
- The smooth forcing helper is climatology, not stochastic/spatial weather.
- No frozen-soil effects, canopy interception, capillary rise, aquifer geometry, lateral groundwater flow, wetlands, floodplains, channel storage, or hydraulic water level.
- One L1 cell uses one generic soil bucket; spatial soil parameters do not exist yet.
- Rivers carry routed volume/discharge but do not yet have width/depth/velocity or flood-stage hydraulics.
- Dynamic coupling across multiple tiles must currently be orchestrated by the caller using the explicit outflow/inflow contract.

These limitations are intentional boundaries for v0.4. The next architectural step should be a simulation clock/scheduler and stable multi-tile dynamic-state ownership before adding erosion or vegetation feedback.
