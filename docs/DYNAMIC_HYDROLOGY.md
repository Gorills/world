# Dynamic hydrology — v0.4

> Historical v0.4 design note. The current v0.9 architecture adds a global day, multiresolution water ownership, spatial soil capacity and authoritative `WeatherState`. The standalone L1 solver and smooth forcing helper remain supported lower-level/legacy paths; current system boundaries are documented in `docs/ARCHITECTURE.md` and `docs/AUDIT_v0.9.md`.

## Scope

v0.4 adds a time-dependent water-cycle state on top of the authoritative L0→L1 drainage topology introduced in v0.3.

The dynamic solver operates on one fixed authoritative 8×8 L1 tile (64 regional cells). It does **not** invent drainage directions: every routed volume follows the tile topology produced by `refine_authoritative_hydrology_tile()`.

This is intentionally a compact bucket model. It was designed to establish conserved state and stable contracts before later soil-property, global scheduler and weather layers were added.

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

`DynamicHydrologyTileState::simulated_days` records how much time that standalone state instance has advanced.

## Forcing contract

Hydrology consumes external hydrometeorological forcing per L1 cell:

- total precipitation depth over the requested step;
- mean air temperature;
- potential evapotranspiration depth over the requested step.

This separation was deliberate and remains the current integration boundary. Since v0.9, authoritative `WeatherState` supplies the same forcing variables at L0 for the global/multiresolution scheduler.

v0.4 includes `make_smooth_climatological_forcing()` as a deterministic climate-only provider for tests, CLI runs and controlled integration work. It is not realistic weather.

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

v0.4 provided this volume/coordinate contract. v0.6 later added the global mixed-resolution scheduler that orchestrates sparse authoritative tiles automatically.

## Identity and safety

`AuthoritativeHydrologyTile` carries the `WorldConfig` identity that produced it. Dynamic tile state carries the same identity.

The solver rejects using a tile/state with a different `World`, preventing a valid-looking call from silently using the wrong bounds, sea level, seed, or hierarchy.

Standalone advance calls also preflight the complete forcing, external-inflow accumulation, representable clock advance, water-depth envelope and routed-discharge diagnostic range before committing state. If validation or a later step fails, the supplied `DynamicHydrologyTileState` is left unchanged. The C ABI exposes the same failure-atomic behavior through `ws_dynamic_hydrology_advance()`.

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

Since v0.8, global and refined authoritative water applies derived spatial soil storage/infiltration scale factors to these reference values. The standalone solver follows the same capacity-aware implementation when supplied through current APIs.

## v0.4 limitations and current status

Several original v0.4 limitations were intentionally resolved by later milestones: global simulation time, sparse multi-tile ownership, persistence, spatial soil properties and transient weather now exist.

Still-current material limitations include:

- no frozen-soil effects, canopy interception, capillary rise, aquifer geometry, lateral groundwater flow, wetlands or floodplains;
- no persistent channel storage/travel time or hydraulic water level;
- one vertically aggregated soil bucket;
- rivers do not yet own width/depth/velocity/flood-stage hydraulic state;
- authoritative weather remains L0-only, so standalone L1 weather downscaling is not yet defined.