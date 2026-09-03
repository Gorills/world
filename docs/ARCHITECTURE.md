# Architecture decisions — v0.7

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

v0.3 established authoritative continental topology. v0.4 added standalone detailed L1 water. v0.5 added complete L0 water history and one global day. v0.6 made sparse L1 tiles true refinements of that evolving history. v0.7 adds the first deterministic static L0/L1 environmental property contract without changing dynamic-water ownership.

## 3. Soil property truth and parent/child equivalence

`SoilProperties` currently contains two positive dimensionless modifiers:

- storage-capacity scale;
- infiltration-capacity scale.

The existing hydrology parameters remain configurable reference values. A property value of `1` means the corresponding reference is unchanged.

The parent L0 property is directly reproducible from world seed + climate coordinate. L1 children have deterministic heterogeneity but are normalized by actual world-overlap area so their area-weighted mean is the direct parent value:

```text
parent_scale = Σ(child_scale × child_overlap_area)
               --------------------------------------
                    Σ(child_overlap_area)
```

This definition also holds for partial parents on configured world boundaries.

Soil properties are derived static world truth and are not serialized. The current field is synthetic/hash-based scaffolding, not measured soil classes or realistic pedology.

v0.7 does not yet feed these scales into the water equations. Establishing the property identity/aggregation contract first keeps the next water-capacity change bounded and testable.

## 4. Water ownership and coarse/fine conservation

Dynamic conserved fields cannot exist as two independently advancing copies.

The v0.6+ water owner has two parent states:

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

Transfer is volume-based through actual overlap areas. Partial world-boundary cells therefore conserve the same quantity as full cells.

Water dynamics still use the existing global reference soil capacity in v0.7. L0→L1 water refinement therefore retains the v0.6 uniform-depth rule for this bounded milestone. The next task will replace that rule only after defining how varying child capacity constrains conservative soil-water redistribution.

## 5. Coordinates

World positions are double-precision meters. Grid coordinates are signed 64-bit integers and use mathematical floor semantics, including negative positions.

Configured world bounds are limited by floating-point precision required by 64 m L2 cells rather than by the much larger formal int64 range.

Raster indexing must reject extreme out-of-range coordinates without signed arithmetic overflow. Continental water, multiresolution refined water and standalone detailed hydrology now use checked lower bounds/representability and unsigned non-negative deltas where required.

## 6. Hydrology representation

Water topology is not represented only as a raster.

The L0 whole-world result owns basin/outlet topology. Fixed 8×8 L1 tiles refine that topology. Hydrology contains raster state plus graph semantics:

- per-cell terrain/fill elevation, local yield, accumulated discharge and downstream coordinate;
- graph-like river edges;
- explicit lake records with downstream connectivity;
- stable cross-tile ingress/outlet edges for authoritative L1 refinement.

The mixed scheduler still uses the immutable L0 drainage DAG as the parent ordering/connection truth. A refined parent replaces the local L0 bucket/routing interior, not the continental downstream relation.

Authoritative refinement also inherits the coarse parent's ocean classification for every active L1 child. The current ownership boundary therefore never creates a mixed land/ocean child mask inside one L0 parent.

## 7. Global time and mixed-resolution scheduling

`MultiresolutionWaterState` embeds the continental state and therefore owns one exact signed 64-bit global simulation day.

Every sparse refined tile records that same day. A mismatch is invalid state.

One daily step processes parents in the authoritative L0 topological order:

```text
unrefined parent
    → coarse bucket step
    → coarse route

refined parent
    ← all upstream channel ingress already collected
    → detailed 8×8 L1 step
    → one external outlet volume
    → parent L0 downstream relation
```

When an upstream parent routes into a refined downstream parent, its channel volume goes to the exact deterministic L1 ingress child. When a refined tile drains to another refined tile, the same connection rule maps outlet to ingress.

A refined parent does not independently execute an L0 bucket step, so upstream water cannot be consumed by both L0 and L1 paths.

## 8. Atomicity and forcing boundary

Hydrology does not own weather. It consumes precipitation, mean temperature and potential evapotranspiration.

The bundled smooth forcing remains deterministic scaffolding. A future WeatherSystem can replace it without changing water ownership.

Mixed-resolution steps validate forcing, clocks, storage and numerical bounds before committing. Coarse state is advanced in a scratch vector and refined tile results are retained in temporary vectors. The authoritative stores and global day are changed only after the full routed day succeeds.

This extends the v0.5 invalid-input atomicity contract across both resolutions.

## 9. Determinism

Static terrain/climate/soil state and hydrology use deterministic hashing and explicit tie-breaking. Continental routing uses a deterministic topological order. Tile boundary connections use deterministic elevation/tie-break rules.

Soil sampling is deterministic for identical world configuration and coordinates. L1 soil normalization reconstructs a bounded 8×8 sibling set and depends only on derived world identity/overlap.

Materialization/aggregation and mixed routing are deterministic for identical world/topology/state input on the tested platforms.

Strict bit-identical cross-platform floating-point determinism remains a future contract decision.

## 10. Persistence

Existing `World::save()` format v2 still stores:

- magic/version;
- world configuration including sea level;
- persistent materialized L2 patches.

It remains compatible with v1 files, which imply sea level 0 m.

Derived soil properties are not serialized because seed + coordinates reproduce them.

Dynamic multiresolution water is an explicit simulation object and uses a separate versioned persistence file. That file stores:

- world identity;
- hydrology parameters;
- exact global day;
- complete L0 dynamic state;
- sparse refined parent ownership and child states.

Derived continental/refined topology is reconstructed from the supplied `World` and authoritative topology instead of serialized as another topology truth.

The loader rejects malformed/truncated data, wrong-world identity, invalid storage, clock mismatches, topology mismatches, duplicate refined parents, contradictory non-zero coarse/refined ownership and trailing bytes.

## 11. Engine boundary

The base C ABI continues to use opaque handles plus POD copy functions so engine bindings do not depend on C++ ABI/STL containers.

Multiresolution water uses a separate opaque `ws_multiresolution_water_state` extension for ownership, materialize/aggregate, state copy, coupled daily stepping and persistence.

v0.7 adds `soil_c_api.h` rather than extending `ws_regional_sample`, preserving the existing POD layout. It reuses the opaque `ws_world` handle and exposes derived soil sampling plus an extension-local error channel.

Game engines render/query the simulation and submit commands; they do not own authoritative state.

## 12. Scaling

The complete world keeps one compact L0 dynamic water state per continental cell. Only selected refined parents allocate 64 detailed L1 water cells plus their fixed authoritative tile topology.

Soil does not add a whole-world stored raster. Parent samples are O(1) derived queries. A regional sample currently reconstructs at most the 64 child raw values required to normalize its parent; that is bounded constant work and can be cached later when hydrology begins consuming the field repeatedly.

Dynamic-water memory therefore still grows approximately with:

```text
all L0 water cells + 64 × active refined parent count
```

rather than all L1 cells in Europe.

The project benchmark uses 449,208 L0 cells and 64 simultaneous refined parents. Measured timings/RSS are environment-specific observations, not API guarantees.

## 13. Current strongest limitation

The spatial soil field now has a stable L0/L1 identity and aggregation rule, but dynamic water does not yet consume it. Storage capacity and infiltration behavior therefore remain homogeneous reference-parameter behavior even where soil queries are heterogeneous.

The next bounded task is capacity-aware hydrology integration while preserving v0.6 water ownership and volume conservation. Hydraulic travel time, flood stage, continuous water-surface elevation across tiles, lateral groundwater, wetlands, erosion, sediment and vegetation feedback remain deferred.
