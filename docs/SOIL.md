# Spatial soil properties (v0.7)

## Purpose

v0.7 introduces the first spatial property field intended to influence future water-bucket behavior without creating another persistent world-state authority.

`SoilProperties` currently contains two dimensionless modifiers:

- `storage_capacity_scale` — future scale for soil storage/field/wilting capacities;
- `infiltration_capacity_scale` — future scale for infiltration capacity.

A value of `1` means the existing configurable hydrology reference parameter is unchanged.

These values are synthetic deterministic scaffolding. They are not measured soil classes, texture fractions, geological reconstruction or a claim of real European pedology.

## Derived world truth

Soil properties are reproducible from world seed and coordinates. Sampling them does not materialize L1/L2 dynamic state and does not change `World::save()`.

The APIs are:

```cpp
World::sample_soil(CellCoord regional_coord)
World::sample_soil(WorldPosition position)
World::sample_climate_soil(CellCoord climate_coord)
```

Regional sampling returns L1 heterogeneity. Climate sampling returns the parent-equivalent L0 property used as the coarse/fine conservation target.

## Parent/child contract

For each L0 climate parent, the L1 raw values are normalized over the actual in-world area of its 8×8 regional children.

For a property scale `s`:

```text
parent_scale = Σ(child_scale × child_overlap_area)
               --------------------------------------
                    Σ(child_overlap_area)
```

This is true for full parents and partial parents cut by configured world bounds.

The implementation constructs it as:

```text
child_scale = parent_scale × child_raw / weighted_mean(child_raw)
```

where the weighted mean uses each child's actual world-overlap area.

This contract matters more than any particular synthetic distribution. A future hydrology step can change how soil-water volume is distributed across children while preserving a well-defined parent-equivalent parameter.

## Determinism and identity

For an identical `WorldConfig`, a coordinate returns the same soil properties without stored state. Changing the world seed changes the field.

The parent field is sampled directly in O(1). Regional sampling currently reconstructs the 8×8 sibling normalization, which is bounded constant work. Future large-scale hydrology integration should cache tile properties where repeated daily access makes that worthwhile rather than changing the property contract.

## C ABI

`soil_c_api.h` is an additive extension rather than a change to the existing `ws_regional_sample` layout.

It exposes:

- `ws_world_sample_soil()` for a world position;
- `ws_world_sample_climate_soil()` for the parent-equivalent L0 value;
- `ws_soil_last_error()` as the extension's error channel.

This preserves the existing base C ABI layout while allowing engine adapters to query the new property field.

## Not yet implemented

v0.7 does **not** yet apply these scales to dynamic water equations. The v0.6 bucket solvers and L0↔L1 materialization still use the existing global hydrology reference parameters.

The next bounded task is capacity-aware hydrology integration: use these scales consistently in L0 and L1 bucket limits and define a conservative soil-water refinement rule when child capacities differ.

Vegetation, erosion, sediment, real soil classes, lateral groundwater and hydraulic channel physics remain deferred.
