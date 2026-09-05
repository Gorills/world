# Changelog

## 0.15.0

- Add sparse persistent settlement/entity authority to SimulationState.
- Add bounded environment-driven settlement suitability and population evolution.
- Add compound checkpoint v2 settlement persistence with v1 migration.
- Add additive settlement C++/C ABI surfaces and advance_day_v3.
- Extend focused regressions and Europe-scale unified checkpoint gate.

## 0.11.0

### Added

- Persistent conserved L0 channel storage inside `MultiresolutionWaterState`; channel water remains part of the existing water authority rather than becoming a fourth simulation/checkpoint component.
- Fixed one-day e-folding linear-reservoir release (`0.6321205588285577` of start-of-day storage) with the structural rule that newly generated or arriving channel water cannot be re-released during the same global day.
- One-L0-edge-per-day channel causality across unrefined routing and refined L1 ingress/outlet boundaries.
- Whole-world conservation accounting including persistent channel storage.
- Multiresolution-water persistence format v3 with one `double` channel volume per L0 cell and explicit v2 → zero-channel migration.
- Read-only C++ per-cell/total channel queries.
- Additive standalone multiresolution-water and unified simulation C ABI channel queries without changing existing C POD layouts or signatures.
- C/C++ regressions for channel ownership across refinement/aggregation, delayed transport, invalid-step atomicity, exact v3 persistence, v2 migration, C ABI persistence and compound-checkpoint future equivalence.
- Europe-scale simulation checkpoint gate requiring non-zero channel storage plus exact equality across all 449,208 L0 channel cells after reload and after one deterministic future day.
- `docs/CHANNEL_TRANSPORT.md` and `docs/AUDIT_v0.11.md`.

### Fixed / hardened

- Fixed a conservation defect found by the existing 60-day weather/multiresolution regression during v0.11 development: processing a downstream cell could overwrite upstream channel volume already accumulated in its scratch next-state. Release now subtracts from the scratch value while remaining based only on immutable start-of-day storage.
- Ocean L0 cells are rejected if persistent channel storage is non-zero.
- Channel storage shape, finiteness and non-negativity are validated before daily mutation and on persistence load.
- Invalid mixed-resolution steps preserve the channel array together with terrestrial coarse/refined state and the global clock.
- Refined-parent outlet water returns to the parent's next-day L0 channel store instead of crossing another L0 edge during the same day.

### Observed Europe-scale checkpoint performance

One GCC Release CI observation on the 449,208-L0 / 64-refined fixture measured approximately:

- unified simulation construction: `815.272 ms`;
- five unified days: `776.130 ms`;
- checkpoint save: `170.951 ms`;
- checkpoint load including topology reconstruction: `939.440 ms`;
- checkpoint size: `21,769,048 bytes` (~20.76 MiB);
- channel storage after five warmup days: `85,772,959,568.875 m³`;
- peak RSS: `238,800 KiB`;
- maximum relative water-balance residual: `5.886e-9`.

These are environment-specific observations, not API guarantees.

### Deliberately unchanged

- The L0 drainage topology and terrestrial bucket equations are unchanged.
- Channel travel time uses one fixed global reservoir coefficient; reach-specific geometry, velocity and residence time are not modeled yet.
- No channel capacity, flood-wave/backwater hydraulics, wetlands/floodplains or lateral groundwater-channel exchange are introduced.
- The compound simulation checkpoint still has exactly World, Weather and Multiresolution Water sections; channel storage lives inside the water section.
- World and Weather persistence formats are unchanged.
- FNV-1a remains corruption detection rather than authentication.
- Binary persistence remains native-POD/non-cross-endian.
- POSIX publication still does not claim full power-loss directory-entry durability because the parent directory is not explicitly fsynced after rename.

## 0.10.0

### Added

- `SimulationState` as the application-level owner of persistent `World` history, derived continental topology, `WeatherState` and `MultiresolutionWaterState`.
- One exact simulation-day invariant across weather, coarse water and every refined water tile.
- Unified day/refinement/aggregation/surface-disturbance command boundary with const component views.
- Versioned compound simulation checkpoint containing World, Weather and Multiresolution Water as one generation while rebuilding topology from World on load.
- Strict checkpoint section ordering/length validation and streaming FNV-1a corruption checksums.
- Validated same-directory temporary checkpoint publication with atomic target replacement; an existing target is never truncated before the replacement generation is complete.
- `SimulationState::from_world()` migration path preserving existing materialized L2 history from pre-v0.10 World saves.
- Opaque `simulation_c_api.h` handle for unified create/query/advance/refine/aggregate/disturb/checkpoint behavior without exposing mutable component handles.
- CLI `simulation-run` and `simulation-resume` commands, including zero-day migration-only checkpoint creation.
- Direct C++ checkpoint regressions for canonical bytes, exact reload/future evolution, corruption, truncation, global/component clock mismatch and replacement.
- C ABI regression for exact weather/coarse/refined checkpoint round-trip and deterministic future equivalence.
- CLI CTest chain covering legacy `demo` save → compound `simulation-run` → in-place `simulation-resume`.
- Europe-scale unified checkpoint benchmark/gate with 449,208 L0 cells, 64 refined parents, persistent L2 history and exact next-day equivalence after reload.
- `docs/SIMULATION.md` and `docs/AUDIT_v0.10.md`.

### Fixed / hardened

- Windows checkpoint durability flush now opens the completed temporary checkpoint with writable access before `FlushFileBuffers`; the full MSVC shared-library suite exercises checkpoint save/load.
- C ABI regional sampling explicitly selects the `WorldPosition` overload instead of relying on an ambiguous braced conversion.
- Compound load reconstructs topology from the checkpoint's World before loading water and rejects component/world identity or global-clock mismatch before exposing state.
- Checkpoint save revalidates the complete assembled container and all section checksums before atomic publication.
- Migration regression verifies exact legacy L2 disturbance history survives the new lifecycle and compound checkpoint.

### Observed Europe-scale checkpoint performance

One GCC Release CI observation on the 449,208-L0 / 64-refined fixture measured approximately:

- unified simulation construction: 818 ms;
- five unified days: 821 ms;
- checkpoint save: 163 ms;
- checkpoint load including topology reconstruction: 919 ms;
- checkpoint size: 18,175,376 bytes (~17.33 MiB);
- peak RSS: 229,872 KiB;
- maximum relative water-balance residual: `5.895e-9`.

These are environment-specific observations, not API guarantees.

### Deliberately unchanged

- Existing World, Weather and Multiresolution Water component formats remain versioned independently and retain their existing semantics.
- Continental topology remains derived rather than becoming serialized authority.
- Existing standalone C++/C APIs and focused CLI solver paths remain available for compatibility and testing.
- FNV-1a is corruption detection, not cryptographic authentication.
- Binary persistence still uses native POD representation; cross-endian save portability is not yet guaranteed.
- POSIX publication does not yet claim full power-loss durability of directory metadata because the parent directory is not explicitly fsynced after rename.
- Persistent in-channel travel-time/flood-wave state, L1 atmospheric downscaling, lateral groundwater, multi-layer soil, wetlands/floodplains, vegetation and erosion remain deferred.

## 0.9.0

### Added

- Explicit whole-world L0 `WeatherState` with one exact integer global day and compact temperature/moisture anomaly state.
- Spatially coherent daily synoptic innovations on an approximately 32 km lattice with temporal and neighboring-cell memory.
- Intermittent precipitation, transient temperature and temperature-driven PET through the existing water forcing boundary.
- Atomic weather-driven stepping for both continental and multiresolution water; weather commits only after the water step succeeds.
- Separate versioned weather persistence that stores transient anomalies while reconstructing derived climate/elevation metadata from `World`.
- Additive `weather_c_api.h` for weather creation, sampling, forcing, stepping, persistence and coupled multiresolution-water advance.
- CLI `weather-water` command for an end-to-end authoritative weather + whole-world water run while retaining the legacy smooth-forcing command.
- Long-run climatology regression covering precipitation-total anchoring, wet/dry intermittency and centered temperature anomalies.
- Europe-scale 449,208-cell / 64-refined-parent / 30-day coupled weather benchmark in GCC CI.
- `docs/WEATHER.md` and `docs/AUDIT_v0.8.md`.

### Fixed / hardened

- The initial storm-intermittency defaults were found by the new 10-year regression to generate only about 64.6% of the static climate precipitation baseline. The storm-intensity default is calibrated to restore approximately 99.97% on that regression fixture without changing wet-area frequency.
- Weather persistence rejects wrong-world files, invalid raster metadata, non-finite/out-of-range anomaly state, truncation and trailing bytes.
- Weather index lookup rejects extreme out-of-range coordinates without signed-overflowing subtraction.
- Coupled weather/water entry points reject different worlds, grid mismatch and clock mismatch before mutation.

### Deliberately unchanged

- Static climate remains derived long-run world truth; weather is a transient layer around it rather than a replacement climate model.
- Refined L1 water receives its parent L0 weather in v0.9; no 1 km atmospheric/orographic downscaling is invented yet.
- Water ownership, routing topology, soil-capacity behavior, multiresolution-water persistence and `World::save()` formats are unchanged.
- Legacy smooth climatological forcing helpers remain available for controlled tests and older simulation paths.
- Channel travel-time/flood-wave state, lateral groundwater, vegetation, erosion and richer atmospheric physics remain deferred.

## 0.8.0

### Added

- Spatial soil modifiers now drive actual L0 and L1 water-bucket behavior.
- Storage-capacity scale consistently modifies soil capacity, field capacity, wilting point and initial soil water.
- Infiltration-capacity scale modifies the daily infiltration limit independently.
- L0 water state caches parent-equivalent soil scales so whole-world daily stepping does not reconstruct L1 normalization.
- Saturation-preserving L0→L1 soil-water refinement under heterogeneous child capacities.
- Local-capacity validation for standalone L1, continental L0, mixed-resolution ownership, aggregation and dynamic-water persistence.
- Dedicated capacity-aware regression coverage for L0/L1 initialization, infiltration response, partial-parent conservation, aggregation, invalid over-capacity state and persistence semantics.
- `docs/AUDIT_v0.7.md` and updated soil documentation.

### Changed

- L1 child soil-water depth is now heterogeneous when child storage capacities differ; refinement preserves parent saturation instead of copying uniform soil depth.
- Multiresolution-water persistence advances to format v2 because local soil-capacity validity semantics changed. Format v1 is explicitly rejected rather than silently reinterpreted.
- Existing C ABI water layouts and `World::save()` persistence remain unchanged.

### Deliberately unchanged

- Snow, surface-water and groundwater refinement still use parent depth because v0.7 introduced no spatial capacity field for those stores.
- Routing topology, channel transfer, weather-forcing boundaries and global time ownership are unchanged.
- Soil modifiers remain synthetic scaffolding rather than measured pedology.

## 0.7.0

### Added

- Deterministic derived `SoilProperties` for regional L1 cells with storage-capacity and infiltration-capacity scale factors.
- O(1) L0 parent-equivalent soil sampling for climate cells.
- L1 soil heterogeneity normalized by actual world-overlap area so its area-weighted child mean reproduces the L0 parent scale, including partial boundary parents.
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
