# Audit of v0.2 regional hydrology

Scope: verify the actual v0.2 source and tests before treating hydrology as a dependency for erosion, soil, vegetation or settlements.

## Baseline

The v0.2 implementation passed its Release C++ and C ABI suites and its existing drainage invariants. The audit did not find a reason to discard Priority-Flood/D8 as the regional algorithmic kernel.

## Findings that changed v0.3 architecture/code

### 1. Query boundaries were part of the physics

v0.2 treats every boundary cell of the requested L1 rectangle as an open outlet. Therefore the same coordinate can receive a different catchment, accumulated discharge or lake topology when the request rectangle changes.

This is acceptable for a bounded analysis kernel, but invalid as permanent world truth.

Fix: v0.3 adds a whole-world L0 solve over configured world bounds. Arbitrary L1 query rectangles are no longer the source of authoritative basin/outlet identity.

### 2. v0.2 catchment IDs were query-local

Catchment IDs were derived from the terminal boundary index of a request. They were deterministic only for that exact request rectangle.

Fix: authoritative basin IDs are now unique deterministic IDs derived from terminal cell position in the complete L0 world raster.

### 3. There was no ocean datum in world state

The terrain comment explicitly said sea level was not modeled. A stable continental drainage model needs an ocean boundary condition that is part of the world, not a per-query option.

Fix: `WorldConfig::sea_level_m` is persistent. Save format advances to v2; v1 loads with its historical implicit datum of `0 m`.

### 4. Regional refinement needed explicit cross-tile edges

The old `HydrologyCell::downstream_coord` always referred to a cell inside one result rectangle. Fixed authoritative tiles need one edge that can leave the current result and enter the next L0 tile.

Fix: hydrology cells now expose `downstream_is_external` in addition to `has_downstream`.

### 5. L0→L1 refinement could otherwise change the water budget

If L0 and L1 independently estimate annual yield from differently sampled elevations, the sum of refined L1 yield is not guaranteed to equal its L0 parent's yield.

Fix: L1 raw water-yield weights use L1 terrain, then normalize to the authoritative parent L0 local yield. Immediate upstream L0 accumulated discharge is injected at deterministic ingress cells. The tile outlet therefore reproduces parent accumulated discharge within floating-point tolerance.

### 6. Probabilistic hashes are unnecessary for authoritative IDs

An initial v0.3 implementation used coordinate hashes for basin/lake IDs. Collision probability was tiny but non-zero, while the bounded raster already provides an exact indexing scheme.

Fix: basin IDs use terminal L0 indices; refined lake IDs use `(parent L0 index × 64 + local anchor + 1)`. IDs are collision-free inside one configured world.

### 7. Partial edge-cell lake area needed clipping

An early v0.3 refinement version counted every L1 lake cell as a full 1 km², including partial cells at non-grid-aligned world bounds.

Fix: lake area/volume at world edges use actual cell/world overlap area.

### 8. Persistence tests encoded a format offset literal

The duplicate-record corruption fixture assumed fixed v1 byte offsets. Adding sea level made the test edit the wrong bytes.

Fix: offsets are derived from field sizes for the current format, and a separate explicit v1 compatibility fixture verifies backward loading.

## v0.2 behavior intentionally retained

`World::analyze_hydrology(HydrologyRequest)` remains available. It is useful for algorithm experiments and bounded local diagnostics. Its result-local catchments/lakes must not be persisted as global truth.

## Strongest remaining limitation after v0.3

The authoritative L0→L1 hierarchy fixes topology and water-budget continuity, but it does not yet solve full hydraulic continuity of water-surface elevation across independently refined tiles. Also, an L0 ocean cell is a coarse binary classification and explicit closed endorheic basins are not yet modeled.

Those are documented constraints, not hidden assumptions for the next soil-water milestone.
