# Multiresolution dynamic water ownership (v0.6)

## Purpose

v0.5 gives every L0 cell a time-dependent water history. v0.6 connects selected authoritative 8×8 L1 tiles to that history without letting the same water exist as two independent truths.

The layer is deliberately limited to ownership, conservative state transfer and coupled daily routing. It does not add spatial soil types, hydraulic channel travel time, floodplains, erosion or vegetation.

## Ownership model

`MultiresolutionWaterState` owns the complete water truth.

For an unrefined parent:

```text
L0 parent stores = authoritative
L1 detailed state = absent
```

For a refined parent:

```text
L0 parent stores = zero
L1 8×8 stores = authoritative
```

The zero L0 parent is not an independently simulated mirror. This makes double-counting structurally visible: a refined parent must not simultaneously contain coarse water stores.

The global clock remains the exact `int64_t` day held by the embedded continental state. Every refined tile records the same day and is rejected if its clock differs.

## L0 → L1 materialization

Materialization first reconstructs the fixed authoritative hydrology tile for the requested L0 parent. It validates world identity, parent coordinate and downstream topology before changing ownership.

For each conserved store — snow, surface water, soil water and groundwater — the current parent depth is assigned to every terrestrial active child. This is conservative because the current bucket parameters are spatially uniform and the sum of actual child world-overlap areas is required to equal the parent overlap area.

For a partial parent on the configured world boundary:

```text
parent volume = parent depth × actual parent overlap area
child volume  = Σ(child depth × actual child overlap area)
```

The transfer requires these areas to agree within floating-point tolerance. The implementation does not invent a spatial soil-capacity field that does not yet exist.

Only after the complete refined state has been constructed and validated is it inserted into the sparse refined map and the parent L0 stores zeroed.

## L1 → L0 aggregation

Aggregation computes the volume of each conserved store independently over actual child overlap areas, converts each total volume back to a parent depth using the parent overlap area, validates capacity/storage bounds, writes the parent state and releases the sparse L1 owner.

Diagnostics such as the latest local ET/runoff are not conserved physical stores and are not volume-aggregated into the parent. The ownership invariant applies to snow, surface water, soil water and groundwater.

## Coupled daily scheduler

The whole mixed-resolution world advances exactly one day per call.

Unrefined cells execute the coarse bucket processes. A refined parent does not execute an independent L0 bucket step; its detailed L1 state is stepped instead.

The routing boundary is explicit:

```text
coarse upstream
      ↓
exact deterministic L1 ingress child
      ↓
refined 8×8 drainage graph
      ↓
refined outlet volume
      ↓
coarse downstream
```

The L0 topological order guarantees that all immediate and transitive coarse upstream contributions reach the refined ingress before that parent is stepped. Refined outlet water is forwarded exactly once along the parent L0 downstream relation.

A refined-to-refined boundary uses the same deterministic tile-connection rule: the first tile's external outlet volume is injected into the destination tile's exact ingress child.

## Atomicity

A mixed-resolution step validates forcing, storage, clocks and numerical bounds before committing state.

Coarse cells are stepped in a scratch vector. Refined tiles are stepped into temporary child-state vectors. The real coarse stores, sparse refined stores and global day are updated only after the complete routed day succeeds.

Therefore an exception during validation, ingress construction or detailed stepping does not leave a partially advanced world.

## Conservation

The daily report uses the same whole-world balance as the coarse solver:

```text
error = storage_before
      + terrestrial_precipitation
      - terrestrial_evapotranspiration
      - terminal_outflow
      - storage_after
```

`storage_before` and `storage_after` include coarse-owned L0 stores plus detailed stores for refined-owned parents, never both independent copies of the same parent water.

Store and balance accumulation uses `double`; persistent per-cell depths remain `float`, so small representation residuals are expected and regression-tested.

## Persistence

Dynamic multiresolution state remains explicit rather than becoming implicit `World` state. v0.6 therefore uses a separate versioned water-state file instead of changing the existing `World::save()` v1/v2 format.

The multiresolution file stores:

- format magic/version;
- complete world identity;
- dynamic hydrology parameters;
- exact global day;
- L0 raster dimensions and all coarse stores/diagnostics;
- sparse refined parent coordinates;
- each refined tile's exact day and 64 child states.

Derived hydrology topology is reconstructed from the supplied `World` and authoritative continental topology on load rather than serialized as a second topology truth.

The loader rejects wrong-world identity, inconsistent dimensions, invalid/non-finite storage, soil-capacity violations, terrestrial water in ocean cells, non-zero coarse stores under a refined parent, clock mismatch, topology-mismatched children, duplicate refined parents, truncated input and unexpected trailing bytes.

## C ABI

`multiresolution_water_c_api.h` exposes an opaque `ws_multiresolution_water_state` with operations for:

- create/destroy;
- query global day and sparse refined ownership;
- materialize and aggregate a parent;
- copy coarse or refined cells into existing POD state structures;
- build smooth daily forcing;
- advance one coupled global day;
- save/load the versioned multiresolution state.

The extension has `ws_multiresolution_last_error()` so it can live in a separate translation unit without changing the existing `ws_last_error()` ABI contract.

## Determinism and tests

Regression coverage includes:

- per-store L0→L1 conservation;
- deterministic refinement and aggregation;
- L0→L1→L0 round trip;
- partial world cells;
- ocean/boundary cases;
- wrong-world and wrong-parent rejection;
- global/refined clock consistency;
- repeated materialize/dematerialize;
- coarse-upstream/refined/downstream transfer;
- absence of independent coarse/fine double-counting;
- invalid-input atomicity;
- persistence corruption/reload cases;
- equivalent C ABI behavior.

## Scaling

Only selected parents allocate 64 detailed cells. The complete world continues to store one compact L0 state per continental cell; v0.6 does not eagerly allocate L1 across Europe.

`worldsim_multiresolution_water_benchmark` exercises the project's 449,208-cell Europe-scale fixture with 64 simultaneous refined tiles. Benchmark values are observations of a concrete runner/environment and are not performance guarantees.

## Deferred

- spatial soil properties and varying child capacities;
- weather beyond the existing forcing boundary;
- channel travel time and flood-wave hydraulics;
- floodplains and wetlands;
- lateral groundwater aquifers;
- erosion/sediment;
- vegetation feedback;
- entities/economy/politics/magic.
