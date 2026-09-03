# Regional hydrology kernel — v0.2 (legacy bounded analysis in v0.3)

## Legacy status in v0.3

This solver remains available for bounded diagnostics, but its rectangle boundary is an open outlet and therefore its catchments are not authoritative world truth. Use `analyze_continental_hydrology()` plus `refine_authoritative_hydrology_tile()` for stable drainage topology. See `CONTINENTAL_HYDROLOGY.md`.

## Purpose

v0.2 establishes deterministic drainage algorithms and water-balance invariants before water is coupled to erosion, soil, vegetation, animals or settlements.

The solver operates on an L1 rectangle of 1024 m cells. It is derived state: running it does not allocate or mutate persistent L2 patches.

## Inputs

For each L1 cell the current model uses:

- procedural terrain elevation;
- L0 annual precipitation;
- L0 mean annual temperature;
- a first-order elevation temperature correction.

The terrain and climate baseline are still synthetic in v0.2.

## Climatic effective water yield

The current annual water balance uses:

`water surplus = precipitation - actual evapotranspiration`

Actual evapotranspiration uses the empirical annual Turc relationship:

`AET = P` when `P/L <= 0.316`

otherwise:

`AET = P / sqrt(0.9 + (P/L)^2)`

with:

`L = 300 + 25*T + 0.05*T^3`

For this simplified annual model, the temperature passed to the Turc polynomial is clamped to 0 C at the cold end. This avoids a non-useful temperature-only extrapolation in sub-zero annual regimes; it does **not** model winter evapotranspiration or snow physics.

Regional temperature is adjusted from the broad climate baseline by 6.5 C/km for positive elevation as a first-order use of the observed global-average environmental lapse rate.

Annual surplus in mm over a 1024x1024 m cell is converted to mean m3/s.

### Important interpretation

Until soil storage, infiltration, groundwater and snow exist, all annual climatic surplus is routed through the surface drainage network as **effective water yield**. `accumulated_discharge_m3_s` is therefore a model flux, not yet a calibrated real river discharge.

## Drainage algorithm

### 1. Priority-Flood depression filling

All analysis-domain boundary cells are inserted as open outlets. Priority-Flood raises closed interior depressions to their minimum spill surface and records a parent route toward an outlet.

### 2. Flow direction

Each interior cell chooses the steepest descending D8 neighbor on the filled surface. If no lower filled neighbor exists, the Priority-Flood parent resolves the flat and guarantees a path toward an outlet.

### 3. Flow accumulation

Every cell begins with its own local effective water yield. Cells are accumulated in reverse drainage rank into their downstream cell.

Primary invariant tested by the suite:

`sum(local effective water yield) ~= sum(discharge leaving boundary outlets)`

within floating-point tolerance.

### 4. Catchments

A cell's current `catchment_id` is the terminal boundary outlet reached by its downstream path. These IDs are deterministic for the same requested rectangle but intentionally are **not global stable basin IDs** yet.

### 5. Lakes

Cells raised by depression filling form depression basins. Connected depression cells sharing a spill surface are grouped. A basin becomes a `LakeInfo` when its maximum depth reaches `lake_min_depth_m`.

A lake record contains:

- stable result-local ID;
- outlet lake cell;
- explicit downstream/outflow cell when it remains inside the analysis domain;
- cell count and area;
- approximate filled volume;
- surface/spill elevation;
- maximum depth.

The depth threshold decides whether the basin is represented as a lake; it does not trim shallower shoreline cells from a qualifying basin.

### 6. River graph

Non-lake cells with accumulated effective discharge at or above `river_threshold_m3_s` are marked as river cells. `RiverSegment` records connect each such cell to its immediate downstream drainage cell.

A river can enter a lake; the lake's explicit outflow metadata reconnects the topology to the downstream drainage path. Hydraulic width, depth and velocity are deliberately absent: inventing them without a calibrated hydraulic-geometry model would create false physical precision.

The river threshold is currently an extraction parameter, not a universal physical channel-initiation law.

## Resource bound

One solve is limited to `kMaxHydrologyCells = 262144` L1 cells (for example 512x512). This keeps an accidental engine/API request from allocating an unbounded temporary raster.

Continent scale will be handled by a stable coarser drainage layer plus L1 refinement, not by raising this limit until all of Europe is one raster solve.

## Critical limitation: analysis-domain boundaries

Every requested rectangle boundary cell is an open outlet. The solver therefore solves a finite drainage domain, not the final persistent world topology.

Consequences:

- upstream discharge from outside the rectangle is absent;
- changing rectangle extent can change catchments;
- changing rectangle extent can change whether a depression drains or forms a lake;
- edge river topology is not authoritative world truth.

Do not persist the v0.2 result as permanent global hydrology.

## Required next integration

Before erosion or soil-water coupling:

1. establish sea/ocean and stable coarse continent-scale drainage topology;
2. assign basin/outlet identity independently of arbitrary query rectangles;
3. supply upstream/boundary conditions to L1 refinement;
4. define stable caching/persistence rules for hydrological consequences.

## Scientific basis used in v0.2

- Barnes, R., Lehman, C., Mulla, D. (2014), *Priority-Flood: An Optimal Depression-Filling and Watershed-Labeling Algorithm for Digital Elevation Models*, Computers & Geosciences 62, 117-127. DOI: 10.1016/j.cageo.2013.04.024.
- Turc annual evapotranspiration/water-balance relationship: `AET = P/sqrt(0.9 + (P/L)^2)`, `L = 300 + 25T + 0.05T^3`, with the dry-limit branch used in hydrology literature.
- NOAA JetStream reports a global-average atmospheric temperature lapse rate of approximately 6.5 C/km; v0.2 uses it only as a first-order elevation correction.

These are defensible simplified approximations, not final climate/hydrology fidelity.
