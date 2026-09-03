# Audit of v0.6 before spatial soil properties

## Scope

The merged v0.6 multiresolution water layer was reviewed before adding environmental heterogeneity. The review focused on:

- final post-merge GCC/Clang/ASan/UBSan status;
- L0/L1 ownership and parent-zero invariant;
- partial-cell and ocean semantics;
- public indexing paths used by old and new detailed hydrology APIs;
- persistence/C ABI boundaries;
- the next dependency required before spatially varying water capacity can be introduced.

## Baseline result

The v0.6 ownership architecture remains the correct dynamic-water boundary. No rewrite of the coarse/fine scheduler or persistence format is required before adding derived environmental properties.

The next physical dependency is a spatial soil property field with an explicit L0↔L1 aggregation contract. Applying that field to dynamic water is intentionally a later bounded step.

## Runtime defect found

### Standalone v0.4 L1 indexing still had signed-overflow paths

The v0.6 work had already hardened `ContinentalWaterState::index_of()` and the new `RefinedWaterTileState::index_of()`, but the older standalone `DynamicHydrologyTileState::index_of()` still subtracted arbitrary signed 64-bit coordinates and derived `climate_coord * 8` without a representability guard.

The same issue could be reached through malformed public `AuthoritativeHydrologyTile` topology when internal downstream coordinates were extreme.

This was fixed separately before v0.7 feature work. The hardened path validates tile-origin multiplication and uses lower-bound checks followed by unsigned coordinate deltas. Dedicated regressions cover `INT64_MIN`, `INT64_MAX`, malformed downstream coordinates and valid round trips under sanitizer CI.

## Coastal refinement check

A possible conservation failure was considered: a coarse land parent could lose water if some refined children were independently classified as ocean.

That failure mode does not exist under the current authoritative topology contract. `refine_authoritative_hydrology_tile()` assigns every active child the parent's `ocean` classification. A parent is therefore uniformly terrestrial or ocean at the ownership boundary, including partial world-boundary cells.

This means v0.6's current uniform-depth materialization remains conservative under the topology it actually owns.

## Soil property decision

The first spatial soil layer should be derived static world truth, not persistent state. Persisting a field that is fully reproducible from world seed and coordinates would create unnecessary storage and another possible source of inconsistency.

v0.7 therefore introduces dimensionless modifiers rather than absolute replacement parameters:

- storage-capacity scale;
- infiltration-capacity scale.

The existing `DynamicHydrologyParameters` remain the configurable reference values.

## Parent/child decision

L1 variation must not make the meaning of the L0 property arbitrary. For every climate parent, the area-weighted mean of active in-world L1 child scales is defined to equal the directly sampled parent-equivalent L0 scale.

Actual overlap area is used, so the same contract holds at partial world boundaries.

This property-level contract is established before changing any water equation. The next bounded task can then define how water stores are redistributed when child capacities differ without simultaneously inventing the spatial field.

## Result

After the standalone-index hardening, no blocking v0.6 defect was found for the soil-property foundation. v0.7 is limited to deterministic spatial property truth, C++/C query surfaces, tests and documentation; hydrology consumption of those properties remains deferred to the next bounded task.
