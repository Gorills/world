# Architecture decisions — v0.5

## 1. Resolution hierarchy

| Level | Resolution | Purpose | Storage |
|---|---:|---|---|
| L0 | 8192 m | climate baseline + authoritative continental drainage + coarse dynamic water history | whole-world derived topology + compact dynamic state |
| L1 | 1024 m | regional terrain, authoritative refinement + detailed dynamic water state | topology derived; detailed state explicit/selective |
| L2 | 64 m | local persistent environmental history | lazy persistent |
| Entity | continuous | people/animals/items/buildings later | future |

The hierarchy is deliberately fixed for now. Configurability would complicate persistence and cross-level contracts before there is evidence it is needed.

## 2. World truth vs materialization

The whole world must have one consistent truth, but does not need uniform stored resolution.

- Static base fields can be reproduced from seed + coordinates.
- Time-dependent state that must exist everywhere is stored at a cheap authoritative resolution.
- Persistent deviations exist at finer levels only where history requires them.
- Querying coarse fields must not materialize local persistent state.
- Materialization itself is persistent and therefore observable.
- External callers do not receive mutable access to persistent C++ structs; mutations go through explicit commands.
- Derived analyses are not promoted to authoritative truth if their result depends on arbitrary query boundaries.

v0.3 established whole-world L0 drainage and fixed L1 refinement. v0.4 added detailed mutable L1 water state. The v0.4 audit showed that an L1-only scheduler cannot preserve history at Europe scale, so v0.5 adds authoritative time-dependent water state at L0 for the whole world.

## 3. Coarse/fine conservation boundary

Current L0/L1 terrain/climate values are coarse samples/proxies. L2 procedural refinement is not yet required to aggregate exactly back to every placeholder field.

Dynamic conserved fields are different. For water, biomass, nutrients and population, parent/child materialization must preserve the authoritative coarse quantity and later aggregation must return it without creation or loss.

v0.5 establishes the parent water history but does not yet implement L0↔L1 conservative state transfer. That is the next required boundary.

## 4. Coordinates

World positions are double-precision meters. Grid coordinates are signed 64-bit integers and use mathematical floor semantics, including negative positions.

Configured world bounds are limited by floating-point spatial precision required by 64 m L2 cells rather than by the much larger formal int64 range.

## 5. Hydrology representation

Water topology is not represented only as a raster.

The L0 whole-world result owns basin/outlet topology. Fixed 8×8 L1 tiles refine that topology. Hydrology contains raster state plus graph semantics:

- per-cell terrain/fill elevation, local yield, accumulated discharge and downstream coordinate;
- graph-like river edges;
- explicit lake records with downstream connectivity;
- stable cross-tile ingress/outlet edges for authoritative L1 refinement.

v0.5 dynamic L0 runoff/baseflow is routed only through this immutable authoritative drainage DAG.

## 6. Global time

`ContinentalWaterState` owns one exact signed 64-bit simulation day for the complete coarse world. Individual L0 cells cannot drift in time independently.

Detailed L1 state from v0.4 still carries its own explicit time because global L0↔L1 scheduling has not yet been connected. A future detailed tile scheduler must align L1 activation/deactivation with the authoritative global day, not introduce another independent world clock.

## 7. Forcing boundary

Hydrology does not own weather. It consumes precipitation, mean temperature and potential evapotranspiration.

The bundled smooth forcing provider is deterministic scaffolding. A future WeatherSystem can produce forcing for land and ocean without special-casing the terrestrial hydrology mask; ocean forcing is accepted but excluded from terrestrial water stores/balance.

A continental daily forcing array is fully validated before mutation so a rejected step cannot leave a partially advanced world.

## 8. Determinism

Static state and hydrology use deterministic hashing and explicit tie-breaking. Continental dynamic routing uses a deterministic topological order. Query order does not affect generated results on the tested platforms.

Strict bit-identical cross-platform floating-point determinism remains a future contract decision.

## 9. Persistence

Save format v2 currently stores:

- magic/version;
- world configuration including sea level;
- persistent materialized L2 patches.

The loader remains compatible with v1 files, which imply sea level 0 m.

Procedural terrain/climate, hydrology topology and v0.5 continental dynamic water state are not yet stored. Save-format integration should be designed together with L0↔L1 ownership so a save cannot contain contradictory coarse and detailed state.

Serialization order of the existing saved state is canonical. The loader rejects malformed counts, duplicate/out-of-bounds patches, invalid normalized values and trailing bytes.

## 10. Engine boundary

The C ABI uses opaque handles plus POD copy functions so engine bindings do not depend on the C++ ABI or STL containers.

Game engines render/query the simulation and submit commands; they do not own authoritative state. The C ABI continental-water handle also retains the parameter set used to create the state, preventing engine callers from accidentally changing those parameters between days.

## 11. Current strongest limitation

The world now has coarse hydrological history and one global coarse day, but detailed L1 tiles are not yet materialized from / aggregated back into that parent history. Until conservative L0↔L1 transfer exists, detailed tiles remain explicit standalone simulations rather than true refinements of the evolving world state.

Hydraulic travel time, flood stage, continuous water-surface elevation across L1 tiles, endorheic-basin specialization, lateral groundwater, wetlands, erosion and vegetation feedback are also deferred.
