# Audit of v0.7 before capacity-aware water integration

## Scope

The merged v0.7 spatial-soil layer was reviewed against the existing L0/L1 dynamic-water contracts before allowing the new property field to affect simulation state.

The review covered:

- whether L0 and L1 can derive compatible effective soil capacities from one world truth;
- initialization semantics;
- L0↔L1 conservation when child capacities differ;
- runtime capacity validation and atomic rejection;
- persistence compatibility;
- existing C/C++ state layouts and routing ownership.

## Baseline result

The v0.7 property contract is sufficient for the next bounded step. Its strongest useful invariant is not the synthetic distribution itself but the exact parent-equivalent relationship: the area-weighted L1 storage-capacity scale reproduces the L0 parent scale, including partial world-boundary parents.

That makes a conservative heterogeneous soil-water transfer possible without an iterative redistribution algorithm or an additional stored property authority.

## Capacity mapping decision

`storage_capacity_scale` applies to:

- soil capacity;
- field capacity;
- wilting point;
- initial soil water.

Using the same storage scale preserves relative field/wilting thresholds and the configured initial saturation.

`infiltration_capacity_scale` applies only to infiltration capacity per day.

Percolation rate, groundwater recession, surface storage and snowmelt remain reference parameters in this milestone because v0.7 does not define spatial properties for them.

## Refinement decision

Uniform parent soil depth is no longer correct once child capacities differ. The chosen transfer preserves saturation:

```text
parent_saturation = parent_soil_water / parent_capacity
child_soil_water  = parent_saturation * child_capacity
```

Since parent capacity is the area-weighted equivalent of child capacities, the resulting child soil-water volumes sum to the parent soil-water volume over the same actual world overlap area.

This is both simpler and stronger than distributing volume first and then repairing over-capacity children: valid parent state cannot overfill a child under the saturation rule.

Snow, surface water and groundwater continue to transfer by parent depth because no new spatial capacity field applies to those stores.

## Runtime validation decision

A state whose soil water exceeds its local effective capacity is invalid and is rejected before bucket mutation. The same rule is used by:

- standalone detailed L1 stepping;
- whole-world L0 stepping;
- mixed-resolution prevalidation;
- materialization and aggregation;
- multiresolution persistence save/load.

A shared internal scaling helper is used so L0, L1 and persistence cannot silently diverge in the parameter formula.

## Persistence decision

The existing multiresolution-water v1 bytes were produced under uniform soil-capacity semantics. Even though the serialized fields can still be parsed, interpreting those water depths under spatial capacity would change the model contract and could make previously valid files locally over-capacity.

Therefore the dynamic multiresolution-water format advances to v2 and explicitly rejects v1. An automatic migration is deliberately not added without a concrete saved-world migration requirement.

`World::save()` is unchanged because soil properties remain derived from `WorldConfig` and no new persistent world field was introduced.

## Compatibility decision

No existing C ABI water POD layout is changed. L0 caches two soil scale floats in private C++ metadata only. Engine-facing create/advance/materialize/aggregate calls retain their existing signatures and now execute the capacity-aware model internally.

## Validation target

The new regression must fail under the old uniform-capacity implementation. It therefore checks:

- scaled L0 and L1 initial soil depths;
- heterogeneous L1 infiltration response;
- parent-equivalent L0 infiltration response;
- genuinely partial-parent saturation-preserving refinement;
- child capacity bounds and soil-volume conservation;
- aggregation back to the parent;
- rejection of an over-capacity L1 state before mutation;
- persistence format v2 and explicit rejection of v1.

## Result

No additional abstraction or property persistence is required for this milestone. The bounded v0.8 work is to connect the already-derived soil field to the existing water buckets and ownership transfer while keeping routing, weather forcing, channel behavior and unrelated environmental systems unchanged.
