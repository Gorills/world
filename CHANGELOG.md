# Changelog

## 0.5.0

### Added

- Authoritative time-dependent water state for every L0 continental cell.
- One exact integer global simulation day shared by the complete coarse world.
- Coarse snow, surface-water, soil-water and groundwater stores.
- Daily rain/snow partition, melt, infiltration, evapotranspiration, percolation, baseflow and quick runoff.
- Deterministic routing of daily runoff/baseflow through the authoritative continental drainage DAG.
- Whole-continent daily water-balance report.
- Cached climate metadata for practical Europe-scale stepping.
- C ABI continental-water state/forcing/advance/copy APIs.
- CLI `continental-water` command.
- Dedicated continental-water regression tests and `docs/CONTINENTAL_WATER.md`.
- v0.4 architecture audit and GitHub Actions CI are retained as the merge baseline.

### Fixed / hardened

- Rejected continental forcing is fully validated before state mutation, making failed daily steps atomic.
- Continental state/topology identity and routing DAG invariants are validated at construction.
- Ocean atmospheric forcing is accepted and ignored by terrestrial accounting instead of forcing WeatherSystem callers to zero ocean precipitation.
- Tests now verify actual topology/state coordinate alignment rather than a vacuous coverage assertion.
- CLI version text updated to v0.5.

## 0.4.0

### Added

- Time-dependent hydrology state on authoritative 8×8 L1 tiles.
- Conserved snow, surface-water, soil-water and groundwater stores.
- Rain/snow partition and degree-day snowmelt.
- Saturation-dependent infiltration into a bounded soil bucket.
- Surface/soil evapotranspiration demand.
- Soil-to-groundwater percolation and groundwater baseflow.
- Quick runoff above local surface-storage capacity.
- Routed dynamic volume/discharge through authoritative L1 drainage topology.
- Explicit external channel inflow by downstream L1 ingress coordinate.
- Per-step water-balance accounting/report.
- Internal <=1-day substepping for multi-day advances.
- Deterministic smooth climatological forcing helper for pre-weather tests/CLI integration.
- C ABI dynamic-hydrology state, forcing, advance and copy APIs.
- CLI `watercycle` command.
- `docs/DYNAMIC_HYDROLOGY.md` and v0.3 audit.

### Fixed / hardened

- `AuthoritativeHydrologyTile` now carries originating world identity.
- Dynamic forcing/state stepping rejects topology/state from a different world configuration.
- Dynamic water stores are validated finite/non-negative and forcing records must align exactly with tile cells.
- Dynamic routing validates the authoritative drainage graph is acyclic.

## 0.3.0

### Added

- Persistent `WorldConfig::sea_level_m` world property.
- Whole-world L0 continental hydrology over configured world bounds.
- L0 ocean classification, global drainage, accumulated discharge and stable terminal outlets.
- Deterministic collision-free world-local basin IDs.
- Bounded continental solve limit of 1,000,000 L0 cells.
- Fixed 8×8 L1 authoritative hydrology refinement per L0 cell.
- Deterministic cross-tile L1 outlet/ingress edges.
- L0 upstream discharge injection into L1 refinement.
- Exact parent/local water-yield conservation at the L0→L1 boundary (within floating-point representation).
- `active`, `ocean` and `downstream_is_external` metadata on hydrology cells.
- C ABI handles/functions for continental hydrology and authoritative L1 tile refinement.
- CLI `continent` and `tile` commands.
- Save format v2; v1 files remain readable with implicit sea level 0 m.
- v0.2 audit and continental-hydrology documentation.

### Fixed / hardened

- Removed query-rectangle catchments from the role of authoritative world drainage truth.
- Basin IDs no longer depend on query-local boundary indices for authoritative hydrology.
- Basin/lake IDs use deterministic collision-free indexing within the configured world instead of probabilistic coordinate hashes.
- Partial world-edge L1 lake area/volume uses actual in-world overlap area rather than a full 1 km² cell.
- Save-format corruption tests derive header offsets from field sizes rather than a magic literal.
- Authoritative refinement rejects continental topology generated for a different world configuration.

## 0.2.0

### Added

- Regional L1 hydrology analysis.
- Priority-Flood depression filling.
- Deterministic D8/flat drainage paths.
- Empirical annual effective climatic water yield and accumulated discharge.
- Domain-local catchment/outlet IDs.
- Lake extraction with area/volume/depth/outlet and explicit outflow metadata.
- River segment graph.
- Per-analysis hydrology cell cap.
- C ABI hydrology result handle and copy functions.
- CLI hydrology summary command.
- Audit and hydrology design documentation.

### Fixed / hardened from 0.1.0

- Zero disturbance no longer materializes L2.
- Disturbance count now reports actual state changes.
- Public C++ materialization no longer exposes mutable persistent state.
- Coordinate conversion validates finite/range/cell-size inputs.
- World bounds use a floating-point precision-safe spatial limit rather than a misleading near-int64 limit.
- Direct hierarchy conversion validates its ratio.
- L0 climate sampling now respects configured world bounds.
- Save loader rejects malformed patch state, duplicate coordinates and trailing data.
- Save loader validates declared patch records against actual file length before reserve/read.
- Save serialization checks the map-key/patch-coordinate invariant.
- Pointer-returning C ABI functions contain unknown exceptions.
- L1 `mean_elevation_m` was renamed to `elevation_m` to match its center-sample semantics.
