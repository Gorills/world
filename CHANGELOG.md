# Changelog

## 0.7.0

### Added

- Deterministic derived `SoilProperties` for regional L1 cells with storage-capacity and infiltration-capacity scale factors.
- O(1) L0 parent-equivalent soil sampling for climate cells.
- L1 soil heterogeneity normalized by actual in-world overlap area so the area-weighted child mean reproduces the L0 parent scale, including partial boundary parents.
- Position- and coordinate-based C++ soil sampling without L1/L2 materialization or persistence.
- Additive soil C ABI sampling in `soil_c_api.h` with an independent error channel.
- Dedicated C++ and C regression coverage for determinism, positive finite scales, seed identity, partial-cell parent/child equivalence and non-materializing queries.
- `docs/SOIL.md` describing the v0.7 property contract.

### Fixed / hardened

- Standalone detailed L1 hydrology now rejects unrepresentable parent coordinates and extreme out-of-range cell/downstream coordinates without signed integer overflow.

### Deliberately unchanged

- v0.7 soil values are synthetic static modifiers, not measured soil classes or pedological reconstruction.
- Water bucket equations and v0.6 L0↔L1 transfer still use their existing global reference parameters in this milestone; applying spatial soil scales to water dynamics is the next bounded task.
- Existing world save v1/v2 and multiresolution-water persistence formats remain unchanged because soil properties are reproducible derived world truth.

## 0.6.0

### Added

- `MultiresolutionWaterState` as the single ownership boundary for coarse L0 and sparse refined L1 dynamic water.
- Conservative L0→L1 transfer for snow, surface water, soil water and groundwater using actual world-overlap area, including partial boundary cells.
- Conservative L1→L0 aggregation and repeated materialize/dematerialize support.
- One exact global integer day across coarse and refined ownership.
- Coupled daily scheduler with deterministic coarse-upstream → refined-ingress and refined-outlet → coarse-downstream transfer.
- Atomic mixed-resolution stepping: rejected input does not partially mutate coarse stores, refined stores or clocks.
- Versioned multiresolution-water persistence containing the global day, coarse state and sparse refined ownership while leaving `World::save()` v1/v2 unchanged.
- C ABI for multiresolution create/destroy, materialize/aggregate, state copy, forcing, daily stepping and persistence.
- Europe-scale mixed-resolution benchmark executable and GCC CI observation step.
- Regression suites for conservation, determinism, partial cells, ocean/boundary cases, state identity, clock consistency, repeated ownership transfer, coupled routing, persistence corruption and C ABI equivalence.
- `docs/MULTIRESOLUTION_WATER.md` and v0.5 audit notes.

### Fixed / hardened

- Continental dynamic-water inputs now reject finite values that can overflow persistent float state or routed-discharge diagnostics before mutation.
- `ContinentalWaterState::index_of()` no longer performs signed-overflowing subtraction for extreme out-of-range coordinates.
- Refined parent L0 water stores are explicitly zero while L1 owns the region, preventing independent double-counted coarse/fine truth.
- Persistence rejects wrong-world files, duplicate refined parents, refined/global clock mismatch, malformed/truncated input and unexpected trailing data.

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
