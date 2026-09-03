# Multiresolution dynamic water ownership (v0.8 terrestrial design)

> Historical v0.8 design note. v0.11 keeps the terrestrial L0↔L1 ownership model below and adds persistent conserved L0 channel storage around it. See `docs/CHANNEL_TRANSPORT.md` and `docs/ARCHITECTURE.md` for the current routing/persistence contract.

## Purpose

v0.6 connected selected authoritative 8×8 L1 tiles to the complete L0 water history without letting the same terrestrial water exist as two independent truths. v0.8 keeps that ownership model and makes soil-water refinement aware of the spatial soil capacities introduced in v0.7.

The terrestrial layer remains deliberately limited to ownership and conservative state transfer. v0.11 channel travel time is a separate store inside the same `MultiresolutionWaterState`; floodplains, erosion and vegetation remain outside this note.

## Ownership model

`MultiresolutionWaterState` owns the complete water truth.

For an unrefined parent:

```text
L0 parent terrestrial stores = authoritative
L1 detailed state            = absent
```

For a refined parent:

```text
L0 parent terrestrial stores = zero
L1 8×8 terrestrial stores    = authoritative
```

The zero L0 parent is not an independently simulated mirror. This makes double-counting structurally visible: a refined parent must not simultaneously contain coarse terrestrial water stores.

Since v0.11, every terrestrial parent also owns one L0 channel volume that remains unchanged by materialization/aggregation. That channel volume is not duplicated into the L1 terrestrial bucket state.

The global clock remains the exact `int64_t` day held by the embedded continental state. Every refined tile records the same day and is rejected if its clock differs.

## L0 → L1 materialization

Materialization reconstructs the fixed authoritative hydrology tile for the requested L0 parent and validates world identity, parent coordinate and downstream topology before changing terrestrial ownership.

Snow, surface water and groundwater copy the current parent depth into every terrestrial active child. Actual child overlap areas are required to sum to the parent overlap area, so these stores conserve volume.

Soil water uses the spatial capacity contract instead. Let:

```text
parent_saturation = parent_soil_water / parent_soil_capacity
```

Then each terrestrial child receives:

```text
child_soil_water = parent_saturation × child_soil_capacity
```

v0.7 guarantees that parent storage-capacity scale is the actual-area-weighted mean of its child storage scales. Therefore parent soil capacity equals the area-weighted child capacity, including partial parents cut by world bounds. The saturation rule consequently conserves soil-water volume by construction.

It also cannot overfill a child when the parent state itself is valid.

Only after the complete refined state has been constructed and validated is it inserted into the sparse refined map and the parent L0 terrestrial stores zeroed. The parent L0 channel storage is left unchanged.

## L1 → L0 aggregation

Aggregation validates every terrestrial child's soil water against its own effective capacity, computes the volume of each conserved terrestrial store independently over actual child overlap areas, and converts each total volume back to parent depth using the parent overlap area.

The aggregated soil depth must fit the parent-equivalent capacity before terrestrial ownership returns to L0.

Diagnostics such as the latest local ET/runoff are not conserved physical stores and are not volume-aggregated into the parent. The terrestrial ownership invariant applies to snow, surface water, soil water and groundwater. The separate v0.11 L0 channel store is unchanged by aggregation.

## Capacity-aware bucket behavior

The same reference hydrology parameters are used at both terrestrial levels. Effective local soil parameters are obtained from derived `SoilProperties`:

```text
soil/field/wilting capacity = reference value × storage_capacity_scale
initial soil water          = reference value × storage_capacity_scale
infiltration capacity       = reference value × infiltration_capacity_scale
```

The L0 state caches its parent-equivalent soil scales. L1 tiles use their derived child properties. Both levels reject soil water above the effective local capacity before stepping.

## Current coupled daily scheduler

The v0.8 routing description is superseded by v0.11 channel transport.

Current behavior is:

```text
terrestrial bucket runoff/outlet
        ↓
parent L0 channel storage
        ↓ later day only
bounded start-of-day channel release
        ↓ at most one L0 edge/day
optional deterministic refined L1 ingress/routing
        ↓
refined parent next-day L0 channel storage
```

A refined parent still executes its detailed L1 terrestrial bucket/routing state instead of an independent L0 terrestrial bucket. The important v0.11 change is that current-day runoff or upstream arrival cannot immediately traverse the remaining L0 DAG.

See `docs/CHANNEL_TRANSPORT.md` for the complete release, refined-ingress and conservation contract.

## Atomicity

A mixed-resolution step validates forcing, storage, clocks, local soil capacity, channel storage and numerical bounds before committing state.

Coarse cells, refined tiles and channel volumes are stepped into scratch state. The real terrestrial stores, channel stores, sparse refined stores and global day are updated only after the complete day succeeds.

Therefore invalid over-capacity storage or an exception during validation, ingress construction or detailed stepping does not leave a partially advanced world.

## Conservation

The daily report uses the whole-world balance:

```text
error = storage_before
      + terrestrial_precipitation
      - terrestrial_evapotranspiration
      - terminal_outflow
      - storage_after
```

Since v0.11, `storage_before` and `storage_after` include coarse-owned L0 terrestrial stores plus detailed stores for refined-owned parents plus all persistent L0 channel storage.

Store and balance accumulation uses `double`; persistent per-cell terrestrial depths remain `float`, so small representation residuals are expected and regression-tested.

## Persistence

Dynamic multiresolution state remains explicit rather than becoming implicit `World` state. It uses a separate versioned water-state file instead of changing `World::save()`.

v0.8 introduced format v2 for spatial soil-capacity semantics. v0.11 advances the current format to v3 by adding one `double` channel volume per L0 cell.

Derived hydrology topology and soil properties are reconstructed from the supplied `World`/`WorldConfig` on load rather than serialized as additional truth.

Format v2 is now a supported migration source: valid v2 terrestrial/refined state loads with zero channel storage because that format had no persistent channel authority. Format v1 remains rejected because its uniform soil-capacity semantics are incompatible with current validity rules.

The current loader rejects wrong-world identity, inconsistent dimensions, invalid/non-finite terrestrial or channel storage, local soil-capacity violations, terrestrial/channel water in ocean cells, non-zero coarse terrestrial stores under a refined parent, clock mismatch, topology-mismatched children, duplicate refined parents, truncated input and unexpected trailing bytes.

## C ABI

`multiresolution_water_c_api.h` exposes an opaque `ws_multiresolution_water_state` with operations for:

- create/destroy;
- query global day and sparse refined ownership;
- query one L0 channel volume or total channel storage;
- materialize and aggregate a parent;
- copy coarse or refined cells into existing POD state structures;
- build smooth daily forcing;
- advance one coupled global day;
- save/load the versioned multiresolution state.

v0.11 adds functions without changing existing water POD layouts or existing signatures.

## Determinism and tests

Regression coverage now includes the earlier capacity/ownership cases plus:

- delayed current-day runoff release;
- refined-parent one-edge channel routing;
- channel ownership unchanged across materialize/aggregate;
- invalid-input atomicity including channel state;
- persistence v3 exact round-trip and explicit v2 → zero-channel migration;
- equivalent C ABI channel behavior;
- weather-driven mixed-resolution conservation;
- compound checkpoint future equivalence including channel state.

## Scaling

Only selected parents allocate 64 detailed terrestrial cells. The complete world stores one compact L0 terrestrial state and one `double` channel volume per continental L0 cell; it does not eagerly allocate L1 across Europe.

`worldsim_multiresolution_water_benchmark` and `worldsim_simulation_benchmark` exercise the 449,208-cell Europe-scale fixture with 64 simultaneous refined tiles. Timing/RSS values are environment-specific observations, not API guarantees; CI enforces correctness/conservation rather than a performance threshold.

## Deferred

- reach-specific channel residence time/geometry/velocity;
- channel capacity and flood-wave/backwater hydraulics;
- floodplains and wetlands;
- lateral groundwater aquifers;
- real soil classes/horizons and measured pedology;
- L1 weather downscaling;
- erosion/sediment;
- vegetation feedback;
- entities/economy/politics/magic.
