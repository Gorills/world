# Architecture decisions — v0.4

## 1. Resolution hierarchy

| Level | Resolution | Purpose | Storage |
|---|---:|---|---|
| L0 | 8192 m | climate baseline + authoritative continental drainage | procedural/derived whole-world solve |
| L1 | 1024 m | regional terrain, authoritative refinement + dynamic water state | topology derived; dynamic state explicit |
| L2 | 64 m | local persistent environmental history | lazy persistent |
| Entity | continuous | people/animals/items/buildings later | future |

The hierarchy is deliberately fixed for now. Configurability would complicate persistence and cross-level contracts before there is evidence it is needed.

## 2. World truth vs materialization

The whole world must have one consistent truth, but does not need uniform stored resolution.

- Static base fields can be reproduced from seed + coordinates.
- Persistent deviations exist only where history changes them.
- Querying coarse fields must not materialize local persistent state.
- Materialization itself is persistent and therefore observable.
- External callers do not receive mutable access to persistent C++ structs; mutations go through explicit commands.
- Derived analyses are not promoted to authoritative truth if their result depends on arbitrary query boundaries.

v0.3 separated the legacy bounded L1 analysis kernel from whole-world L0 drainage and fixed L1 refinement tiles. v0.4 adds mutable dynamic water state only on top of those fixed authoritative tiles.

## 3. Coarse samples vs future aggregates

Current L0/L1 terrain/climate values are coarse samples/proxies. L2 procedural refinement is not yet required to aggregate exactly back to every L1 placeholder field.

When dynamic fields such as soil water, biomass, nutrients and population are introduced, parent/child aggregation becomes an explicit invariant: materializing detail must conserve the coarse state, and changing detail must update the coarse state.

This distinction prevents treating temporary scaffolding (`forest_potential`) as if it were already a conserved ecological quantity.

## 4. Coordinates

World positions are double-precision meters. Grid coordinates are signed 64-bit integers and use mathematical floor semantics, including negative positions.

Configured world bounds are limited by floating-point spatial precision required by 64 m L2 cells rather than by the much larger formal int64 range.

## 5. Hydrology representation

Water topology is not represented only as a raster.

Hydrology uses a hierarchy rather than one query rectangle. The L0 whole-world result owns basin/outlet topology; fixed 8×8 L1 tiles refine that topology. Regional hydrology results contain:

- raster per-cell state: terrain/fill elevation, local water yield, accumulated discharge, downstream coordinate, lake/catchment IDs;
- graph-like river edges;
- explicit lake records with downstream connectivity.

This preserves the design rule that connected narrow features such as rivers need topology/graph semantics in addition to fields.

## 6. Determinism

Static state and hydrology use deterministic hashing and explicit tie-breaking. Query order does not affect generated results on the tested platforms.

Strict bit-identical cross-platform floating-point determinism remains a future contract decision.

## 7. Persistence

Save format v2 stores:

- magic/version;
- world configuration including sea level;
- persistent materialized L2 patches.

The loader remains compatible with v1 files, which imply sea level 0 m.

Procedural terrain/climate and hydrology results are reconstructed/derived and are not stored.

Serialization order is canonical. The loader rejects malformed counts, duplicate/out-of-bounds patches, invalid normalized values and trailing bytes.

The format remains native-endian and is not yet promised as the permanent public save format.

## 8. Engine boundary

The C ABI uses opaque handles plus POD copy functions so engine bindings do not depend on the C++ ABI or STL containers.

Game engines render/query the simulation and submit commands; they do not own authoritative state.

## 9. Dynamic state boundary

v0.4 intentionally keeps forcing separate from hydrology. Hydrology consumes precipitation, temperature and PET records; the bundled smooth climatological provider is scaffolding and can later be replaced by a weather system without changing the storage/routing contract.

Dynamic tile state is not yet stored inside `World`. This avoids inventing a global-time ownership model prematurely. The next foundation is a simulation clock and scheduler that can own, lazily advance, persist and cross-couple many tile states deterministically.

## 10. Current strongest limitation

Topology and water accounting are stable, but there is no global multi-tile time scheduler/persistence yet, and hydraulic surface elevation is not solved continuously across independently refined L1 tiles. Coastline is also binary at L0 resolution and endorheic basins are not a dedicated physical class yet.
