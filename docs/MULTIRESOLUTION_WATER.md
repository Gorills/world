# Multiresolution dynamic water ownership (v0.8)

## Purpose

v0.6 connected selected authoritative 8×8 L1 tiles to the complete L0 water history without letting the same water exist as two independent truths. v0.8 keeps that ownership model and makes soil-water refinement aware of the spatial soil capacities introduced in v0.7.

The layer remains deliberately limited to ownership, conservative state transfer and coupled daily routing. It does not add hydraulic channel travel time, floodplains, erosion or vegetation.

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

Materialization reconstructs the fixed authoritative hydrology tile for the requested L0 parent and validates world identity, parent coordinate and downstream topology before changing ownership.

Snow, surface water and groundwater still copy the current parent depth into every terrestrial active child. Actual child overlap areas are required to sum to the parent overlap area, so these stores conserve volume.

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

Only after the complete refined state has been constructed and validated is it inserted into the sparse refined map and the parent L0 stores zeroed.

## L1 → L0 aggregation

Aggregation validates every terrestrial child's soil water against its own effective capacity, computes the volume of each conserved store independently over actual child overlap areas, and converts each total volume back to parent depth using the parent overlap area.

The aggregated soil depth must fit the parent-equivalent capacity before ownership returns to L0.

Diagnostics such as the latest local ET/runoff are not conserved physical stores and are not volume-aggregated into the parent. The ownership invariant applies to snow, surface water, soil water and groundwater.

## Capacity-aware bucket behavior

The same reference hydrology parameters are used at both levels. Effective local soil parameters are obtained from derived `SoilProperties`:

```text
soil/field/wilting capacity = reference value × storage_capacity_scale
initial soil water          = reference value × storage_capacity_scale
infiltration capacity       = reference value × infiltration_capacity_scale
```

The L0 state caches its parent-equivalent soil scales. L1 tiles use their derived child properties. Both levels reject soil water above the effective local capacity before stepping.

## Coupled daily scheduler

The whole mixed-resolution world advances exactly one day per call.

Unrefined cells execute the capacity-aware coarse bucket processes. A refined parent does not execute an independent L0 bucket step; its detailed L1 state is stepped instead.

The routing boundary is unchanged:

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

A mixed-resolution step validates forcing, storage, clocks, local soil capacity and numerical bounds before committing state.

Coarse cells are stepped in a scratch vector. Refined tiles are stepped into temporary child-state vectors. The real coarse stores, sparse refined stores and global day are updated only after the complete routed day succeeds.

Therefore invalid over-capacity storage or an exception during validation, ingress construction or detailed stepping does not leave a partially advanced world.

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

Store and balance accumulation uses `double`; persistent per-cell depths remain `float`, so small representation residuals are expected and regression-tested. Heterogeneous soil transfer has a looser absolute float-representation tolerance than the unchanged uniform-depth stores but remains constrained by relative conservation.

## Persistence

Dynamic multiresolution state remains explicit rather than becoming implicit `World` state. It uses a separate versioned water-state file instead of changing `World::save()`.

v0.8 writes multiresolution-water format v2. The file stores:

- format magic/version;
- complete world identity;
- reference dynamic hydrology parameters;
- exact global day;
- L0 raster dimensions and all coarse stores/diagnostics;
- sparse refined parent coordinates;
- each refined tile's exact day and 64 child states.

Derived hydrology topology and soil properties are reconstructed from the supplied `World`/`WorldConfig` on load rather than serialized as additional truth.

Format v1 was written under uniform soil-capacity semantics. Although its byte fields can be parsed, silently treating those depths as spatial-capacity state would change validity semantics. v0.8 therefore rejects v1 explicitly and provides no automatic migration.

The v2 loader rejects wrong-world identity, inconsistent dimensions, invalid/non-finite storage, local soil-capacity violations, terrestrial water in ocean cells, non-zero coarse stores under a refined parent, clock mismatch, topology-mismatched children, duplicate refined parents, truncated input and unexpected trailing bytes.

## C ABI

`multiresolution_water_c_api.h` continues to expose an opaque `ws_multiresolution_water_state` with operations for:

- create/destroy;
- query global day and sparse refined ownership;
- materialize and aggregate a parent;
- copy coarse or refined cells into existing POD state structures;
- build smooth daily forcing;
- advance one coupled global day;
- save/load the versioned multiresolution state.

No water POD layout changes in v0.8. Capacity-aware semantics are implemented behind the existing interfaces.

## Determinism and tests

Regression coverage includes:

- scaled L0/L1 initial soil water;
- parent and child infiltration-scale response;
- saturation-preserving heterogeneous soil refinement;
- partial-parent soil-volume conservation;
- child and parent capacity bounds;
- deterministic refinement and aggregation;
- L0→L1→L0 round trip;
- ocean/boundary cases;
- wrong-world and wrong-parent rejection;
- global/refined clock consistency;
- repeated materialize/dematerialize;
- coarse-upstream/refined/downstream transfer;
- absence of independent coarse/fine double-counting;
- invalid-input atomicity;
- persistence v2 round trip/corruption cases and explicit v1 rejection;
- equivalent C ABI behavior.

## Scaling

Only selected parents allocate 64 detailed cells. The complete world continues to store one compact L0 state per continental cell and does not eagerly allocate L1 across Europe.

v0.8 adds two cached soil-scale floats to private L0 metadata so coarse daily stepping remains O(number of L0 cells) without reconstructing L1 soil normalization each day.

`worldsim_multiresolution_water_benchmark` continues to exercise the 449,208-cell Europe-scale fixture with 64 simultaneous refined tiles. Timing/RSS values are environment-specific observations, not API guarantees; CI enforces correctness/conservation rather than a performance threshold.

## Deferred

- real soil classes/horizons and measured pedology;
- weather beyond the existing forcing boundary;
- channel travel time and flood-wave hydraulics;
- floodplains and wetlands;
- lateral groundwater aquifers;
- erosion/sediment;
- vegetation feedback;
- entities/economy/politics/magic.
