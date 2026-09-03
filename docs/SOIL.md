# Spatial soil properties and water capacity (v0.8)

## Purpose

v0.7 introduced deterministic spatial soil modifiers. v0.8 makes those modifiers part of the actual L0/L1 water-bucket model while retaining one conservative parent/child water truth.

`SoilProperties` contains two dimensionless modifiers:

- `storage_capacity_scale` — scales soil capacity, field capacity and wilting point;
- `infiltration_capacity_scale` — scales infiltration capacity per day.

A value of `1` leaves the corresponding configurable hydrology reference parameter unchanged.

These values remain synthetic deterministic scaffolding. They are not measured soil classes, texture fractions, geological reconstruction or a claim of real European pedology.

## Derived world truth

Soil properties remain reproducible from world seed and coordinates. Sampling them does not materialize L1/L2 dynamic state and does not change `World::save()`.

The APIs are:

```cpp
World::sample_soil(CellCoord regional_coord)
World::sample_soil(WorldPosition position)
World::sample_climate_soil(CellCoord climate_coord)
```

Regional sampling returns L1 heterogeneity. Climate sampling returns the parent-equivalent L0 property.

## Effective bucket parameters

For a cell with storage scale `s_storage` and infiltration scale `s_infiltration`:

```text
soil_capacity        = reference_soil_capacity        × s_storage
field_capacity       = reference_field_capacity       × s_storage
wilting_point        = reference_wilting_point        × s_storage
initial_soil_water   = reference_initial_soil_water   × s_storage
infiltration_capacity= reference_infiltration_capacity× s_infiltration
```

Scaling field capacity and wilting point with storage capacity preserves their relative positions inside the bucket. Scaling initial soil water by the same factor preserves the configured reference saturation.

Snow storage, surface storage, groundwater recession, percolation rate and snowmelt parameters are not soil-scaled in this milestone.

## Parent/child capacity contract

For each L0 climate parent, v0.7 guarantees the area-weighted L1 storage scale equals the parent storage scale over actual in-world child overlap areas:

```text
parent_scale = Σ(child_scale × child_overlap_area)
               --------------------------------------
                    Σ(child_overlap_area)
```

Therefore the same relation holds for soil capacity.

When an L0 parent is refined, v0.8 preserves parent soil saturation rather than copying parent soil depth uniformly:

```text
parent_saturation = parent_soil_water / parent_soil_capacity
child_soil_water  = parent_saturation × child_soil_capacity
```

Because parent capacity is the area-weighted equivalent of child capacities, this rule simultaneously:

- conserves soil-water volume across the L0→L1 boundary;
- keeps every child at the same saturation as the parent at the moment of refinement;
- cannot overfill a child bucket when the parent itself is valid;
- creates real heterogeneous L1 soil-water depths when child capacities differ.

L1→L0 aggregation remains volume-based over actual child overlap areas and validates the resulting parent depth against the parent-equivalent capacity.

Snow, surface water and groundwater continue to transfer by uniform parent depth because this milestone introduces spatial heterogeneity only for soil bucket capacity.

## Runtime validation

Before water mutation, L0 and L1 stepping reject soil-water states above the effective local capacity. Materialization, aggregation and multiresolution persistence apply the same local-capacity rule.

The L0 solver caches its parent-equivalent soil scales in compact per-cell metadata so daily whole-world stepping does not reconstruct L1 sibling normalization. L1 tiles sample their bounded 8×8 derived soil field when needed.

## Persistence

`World::save()` is unchanged because soil properties are derived world truth.

Multiresolution dynamic-water persistence changes from format v1 to v2. The serialized state fields are otherwise unchanged, but the validity semantics are different: v1 represented uniform soil capacities, while v2 validates water against spatial local capacity.

The v2 loader therefore rejects v1 files explicitly instead of silently reinterpreting old water depths under a different physical model. No automatic migration is provided in this bounded milestone.

## Determinism and C ABI

Soil property identity remains determined by `WorldConfig`. Existing water C ABI structures are unchanged; capacity-aware behavior is applied behind the existing state/advance/materialize interfaces.

`soil_c_api.h` remains the additive query extension for direct soil-property sampling.

## Current limitations

- Soil modifiers are synthetic rather than measured pedology.
- Soil storage is still a single vertically aggregated bucket; there are no soil horizons or texture-dependent retention curves.
- Infiltration remains a bounded bucket flux rather than Richards-equation flow.
- No lateral groundwater, wetlands, floodplains, erosion, sediment or vegetation feedback is modeled yet.
- Multiresolution water persistence v1 is intentionally not migrated automatically to v2.
