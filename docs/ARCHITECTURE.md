# Architecture decisions — v0.8

## 1. Resolution hierarchy

| Level | Resolution | Purpose | Storage |
|---|---:|---|---|
| L0 | 8192 m | climate baseline + authoritative continental drainage + default dynamic water history + parent-equivalent soil properties | whole-world derived topology + compact dynamic state |
| L1 | 1024 m | regional terrain, authoritative refinement + selectively authoritative detailed water + spatial soil heterogeneity | topology/properties derived; dynamic state sparse |
| L2 | 64 m | local persistent environmental history | lazy persistent |
| Entity | continuous | people/animals/items/buildings later | future |

The hierarchy remains fixed. Configurability is deferred until there is a concrete requirement that outweighs the extra persistence and cross-level complexity.

## 2. World truth vs materialization

The whole world has one truth but does not need one resolution everywhere.

- Static base fields can be reproduced from seed + coordinates.
- Time-dependent state that must exist everywhere is kept cheaply at L0 unless a parent is explicitly refined.
- Persistent or active fine detail exists only where history requires it.
- Querying climate, terrain or soil properties does not materialize L1/L2 state.
- Materializing dynamic water changes ownership and is therefore observable simulation state.
- External callers do not receive mutable access to authoritative C++ containers; mutations go through commands.
- Derived analyses are not promoted to authoritative truth if they depend on arbitrary query boundaries.

v0.3 established authoritative continental topology. v0.4 added standalone detailed L1 water. v0.5 added complete L0 water history and one global day. v0.6 made sparse L1 tiles true refinements of that evolving history. v0.7 added deterministic static L0/L1 soil properties. v0.8 makes those properties part of the bucket model without adding another state authority.

## 3. Soil property truth and effective bucket parameters

`SoilProperties` contains two positive dimensionless modifiers:

- storage-capacity scale;
- infiltration-capacity scale.

The hydrology parameters remain configurable reference values. Effective local values are derived as:

```text
soil_capacity         = reference_soil_capacity         × storage_scale
field_capacity        = reference_field_capacity        × storage_scale
wilting_point         = reference_wilting_point         × storage_scale
initial_soil_water    = reference_initial_soil_water    × storage_scale
infiltration_capacity = reference_infiltration_capacity × infiltration_scale
```

Scaling the three storage thresholds and initial water together preserves their relative bucket geometry and configured initial saturation.

The parent L0 property is directly reproducible from seed + climate coordinate. L1 children have deterministic heterogeneity but are normalized by actual world-overlap area so their area-weighted mean is the direct parent value:

```text
parent_scale = Σ(child_scale × child_overlap_area)
               --------------------------------------
                    Σ(child_overlap_area)
```

This definition also holds for partial parents on configured world boundaries.

Soil properties are derived static world truth and are not serialized. The field remains synthetic/hash-based scaffolding, not measured soil classes or realistic pedology.

## 4. Water ownership and coarse/fine conservation

Dynamic conserved fields cannot exist as two independently advancing copies.

The water owner has two parent states:

```text
coarse-owned:
    L0 parent stores authoritative
    L1 dynamic state absent

refined-owned:
    L0 parent stores zero
    L1 8×8 stores authoritative
```

A non-zero aggregate L0 mirror is deliberately not used for refined parents. Keeping the parent stores zero makes accidental double-stepping visible and prevents coarse water from being mistaken for an independent second copy.

Materialization and aggregation conserve these stores separately:

- snow water equivalent;
- surface water;
- soil water;
- groundwater.

Transfer uses actual world-overlap area. Partial world-boundary cells therefore obey the same quantity contract as full cells.

For soil water, uniform parent depth is no longer valid because child capacities differ. v0.8 uses saturation-preserving refinement:

```text
parent_saturation = parent_soil_water / parent_soil_capacity
child_soil_water  = parent_saturation × child_soil_capacity
```

The v0.7 parent-equivalent capacity invariant makes this volume-conservative by construction. A valid parent cannot produce an over-capacity child. Aggregation remains volume-based and validates the parent-equivalent capacity before ownership returns to L0.

Snow, surface water and groundwater continue to transfer by parent depth because no spatial capacity field currently applies to those stores.

## 5. Coordinates

World positions are double-precision meters. Grid coordinates are signed 64-bit integers and use mathematical floor semantics, including negative positions.

Configured world bounds are limited by floating-point precision required by 64 m L2 cells rather than by the much larger formal int64 range.

Raster indexing must reject extreme out-of-range coordinates without signed arithmetic overflow. Continental water, multiresolution refined water and standalone detailed hydrology use checked lower bounds/representability and unsigned non-negative deltas where required.

## 6. Hydrology representation

Water topology is not represented only as a raster.

The L0 whole-world result owns basin/outlet topology. Fixed 8×8 L1 tiles refine that topology. Hydrology contains raster state plus graph semantics:

- per-cell terrain/fill elevation, local yield, accumulated discharge and downstream coordinate;
- graph-like river edges;
- explicit lake records with downstream connectivity;
- stable cross-tile ingress/outlet edges for authoritative L1 refinement.

The mixed scheduler still uses the immutable L0 drainage DAG as the parent ordering/connection truth. A refined parent replaces the local L0 bucket/routing interior, not the continental downstream relation.

Authoritative refinement inherits the coarse parent's ocean classification for every active L1 child. The current ownership boundary therefore never creates a mixed land/ocean child mask inside one L0 parent.

## 7. Global time and mixed-resolution scheduling

`MultiresolutionWaterState` embeds the continental state and therefore owns one exact signed 64-bit global simulation day.

Every sparse refined tile records that same day. A mismatch is invalid state.

One daily step processes parents in the authoritative L0 topological order:

```text
unrefined parent
    → capacity-aware coarse bucket step
    → coarse route

refined parent
    ← all upstream channel ingress already collected
    → capacity-aware detailed 8×8 L1 step
    → one external outlet volume
    → parent L0 downstream relation
```

When an upstream parent routes into a refined downstream parent, its channel volume goes to the exact deterministic L1 ingress child. When a refined tile drains to another refined tile, the same connection rule maps outlet to ingress.

A refined parent does not independently execute an L0 bucket step, so upstream water cannot be consumed by both L0 and L1 paths.

## 8. Atomicity and forcing boundary

Hydrology does not own weather. It consumes precipitation, mean temperature and potential evapotranspiration.

The bundled smooth forcing remains deterministic scaffolding. A future WeatherSystem can replace it without changing water ownership.

L0 and L1 steps validate local soil capacity before mutation. Mixed-resolution steps validate forcing, clocks, storage, local capacities and numerical bounds before committing. Coarse state is advanced in a scratch vector and refined tile results are retained in temporary vectors. The authoritative stores and global day are changed only after the full routed day succeeds.

## 9. Determinism

Static terrain/climate/soil state and hydrology use deterministic hashing and explicit tie-breaking. Continental routing uses a deterministic topological order. Tile boundary connections use deterministic elevation/tie-break rules.

Soil sampling is deterministic for identical world configuration and coordinates. L1 soil normalization reconstructs a bounded 8×8 sibling set and depends only on derived world identity/overlap.

L0 caches parent-equivalent soil scales at water-state construction. L1 derives child properties from the same `WorldConfig`, so materialization, stepping and persistence validation share one property identity.

Strict bit-identical cross-platform floating-point determinism remains a future contract decision.

## 10. Persistence

Existing `World::save()` format v2 still stores world configuration and persistent materialized L2 patches and remains compatible with v1 files, which imply sea level 0 m.

Derived soil properties are not serialized because seed + coordinates reproduce them.

Dynamic multiresolution water is a separate explicit simulation object. v0.8 writes multiresolution-water format v2 containing:

- world identity;
- reference hydrology parameters;
- exact global day;
- complete L0 dynamic state;
- sparse refined parent ownership and child states.

The serialized water fields are not expanded for soil properties; effective capacities are re-derived from world identity. The format version changes because validity semantics changed from one global soil capacity to spatial local capacity.

Format v1 is explicitly rejected instead of silently reinterpreted. The loader also rejects malformed/truncated data, wrong-world identity, invalid or over-capacity local storage, clock mismatches, topology mismatches, duplicate refined parents, contradictory non-zero coarse/refined ownership and trailing bytes.

## 11. Engine boundary

The base C ABI continues to use opaque handles plus POD copy functions so engine bindings do not depend on C++ ABI/STL containers.

Multiresolution water uses a separate opaque `ws_multiresolution_water_state` extension for ownership, materialize/aggregate, state copy, coupled daily stepping and persistence.

`soil_c_api.h` remains an additive query extension. v0.8 does not change existing water POD layouts or function signatures; capacity-aware behavior is internal to the existing state operations.

Game engines render/query the simulation and submit commands; they do not own authoritative state.

## 12. Scaling

The complete world keeps one compact L0 dynamic water state per continental cell. L0 metadata now caches two soil scale floats per cell to avoid reconstructing L1 normalization during every coarse daily step.

Only selected refined parents allocate 64 detailed L1 water cells plus their fixed authoritative tile topology. Soil still does not add a persistent whole-world fine raster.

Dynamic-water memory therefore remains approximately:

```text
all L0 water/metadata cells + 64 × active refined parent count
```

rather than all L1 cells in Europe.

The project benchmark uses 449,208 L0 cells and 64 simultaneous refined parents. Measured timings/RSS are environment-specific observations, not API guarantees.

## 13. Current strongest limitation

Spatial soil capacity now affects actual bucket behavior and conservative resolution transfer. The strongest remaining model simplifications are outside this bounded property integration: atmospheric forcing is still smooth climatological scaffolding, channel routing has no travel-time/flood-wave state, soil is one vertical bucket, and lateral groundwater is absent.

A post-v0.8 audit should choose the next dependency rather than automatically expanding into vegetation or erosion.
