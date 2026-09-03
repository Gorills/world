# Continental and authoritative regional hydrology — v0.3

## Goal

Provide one stable drainage topology for the configured world so later systems can depend on water without their truth changing when a client asks for a differently sized rectangle.

## L0 raster

Resolution: `8192 m` climate cells.

The complete set of L0 cells intersecting `WorldConfig::bounds` is solved in one bounded derived analysis. `kMaxContinentalHydrologyCells = 1,000,000` prevents accidental unbounded allocation.

Each L0 cell stores:

- representative terrain elevation;
- ocean flag (`elevation <= sea_level_m`);
- depression-filled elevation;
- local annual effective water yield;
- accumulated discharge;
- D8 downstream cell if present;
- terminal outlet coordinate;
- stable basin ID;
- coarse river flag.

## Ocean and external sinks

Ocean cells are terminal absorbing cells at `sea_level_m` and generate no land runoff. Configured world-boundary land cells may also become terminal outlets when they have no lower in-world drainage route.

Priority-Flood seeds both ocean and world-boundary cells. Unlike the old rectangular L1 kernel, a boundary land cell is still allowed to flow inward when a lower in-world neighbor exists.

## Basin IDs

Every terminal L0 cell has a unique row-major index in the complete world L0 raster. Its basin ID is `terminal_index + 1`; `0` remains reserved.

All upstream cells inherit that exact ID and terminal outlet coordinate. No hash collision is possible within a result.

## Water balance

Annual effective water yield remains the v0.2 approximation:

```text
surplus = precipitation - Turc_AET
Q_local = surplus_depth × intersected_cell_area / seconds_per_year
```

Partial L0 cells at non-aligned world bounds use only their actual in-world area.

Accumulation is performed in reverse Priority-Flood rank. The tested invariant is:

```text
sum(all L0 local land yield)
≈
sum(accumulated discharge at all terminal ocean/boundary cells)
```

## L1 authoritative tile refinement

One L0 cell maps to exactly `8 × 8` regional cells because `8192 / 1024 = 8`.

A land tile has exactly one authoritative outlet:

- if its L0 parent flows to another L0 cell, the source/destination L1 edge pair is chosen deterministically on their shared edge/corner;
- if the parent is a terminal boundary land cell, the lowest active L1 cell touching the configured world boundary is the terminal outlet;
- an ocean L0 tile is returned as ocean cells with no land drainage.

The local Priority-Flood is seeded from that fixed outlet, so asking for other tiles cannot move the tile outlet.

## Cross-tile discharge

For every immediate upstream L0 neighbor whose downstream is the current parent, its full L0 accumulated discharge is injected into the deterministic L1 ingress cell.

Raw L1 local yields are computed from L1 terrain and clipped cell area, then scaled so:

```text
sum(L1 local yield in tile) == parent L0 local yield
```

Therefore:

```text
L1 outlet accumulated discharge ≈ parent L0 accumulated discharge
```

The outlet cell uses:

```text
has_downstream = true
downstream_is_external = true
downstream_coord = ingress cell in downstream tile
```

Consumers must not call `HydrologyResult::cell(downstream_coord)` when `downstream_is_external` is true.

## Lakes

Depression components inside one L1 tile are extracted on the locally filled surface. Lake IDs are collision-free inside the world:

```text
lake_id = parent_L0_flat_index * 64 + local_component_anchor + 1
```

World-edge partial cells contribute only their actual in-world area to lake area and volume.

## What is authoritative now

Authoritative:

- ocean vs coarse land at L0;
- L0 downstream topology;
- L0 terminal outlet and basin identity;
- L0 accumulated climatological discharge;
- L1 tile outlet/ingress topology;
- L0→L1 water-budget boundary condition.

Not authoritative yet:

- exact coastline below 8 km;
- exact river width/depth;
- seasonal discharge hydrographs;
- groundwater/baseflow;
- floodplains;
- cross-tile hydraulic head continuity;
- explicit closed endorheic-basin physics.
