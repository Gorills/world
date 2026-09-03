# v0.9 full architecture audit

## Scope

This audit reviews the accumulated v0.1-v0.9 architecture after introducing authoritative daily weather. It is intentionally broader than a feature review: the goal is to identify the strongest state, ownership, persistence, ABI, numerical and scaling risks before selecting the next subsystem.

Audited boundaries:

- coordinate hierarchy and procedural static truth;
- persistent L2 world state;
- continental and refined drainage topology;
- coarse/refined dynamic-water ownership;
- spatial soil-capacity semantics;
- WeatherState ownership and climate anchoring;
- global-day and coupled-step atomicity;
- dynamic persistence and recovery behavior;
- C and C++ API contracts;
- build/platform coverage;
- Europe-scale cost and explicit unverified limits.

## 1. Stable architecture established through v0.9

### Static world truth

Terrain, climate and soil remain deterministic derived fields of world identity and coordinates. Querying them does not materialize L1/L2 persistent state.

The fixed hierarchy remains:

```text
L0  8192 m: climate baseline, weather, continental topology, default water
L1  1024 m: regional terrain, drainage refinement, sparse detailed water, soil heterogeneity
L2    64 m: lazy persistent local environmental history
```

World-coordinate validation is bounded by double-precision requirements rather than the formal int64 grid range. Index-facing APIs that previously risked signed subtraction overflow have dedicated regression coverage.

### Dynamic water truth

`MultiresolutionWaterState` remains the authoritative water owner.

```text
coarse-owned parent:
    L0 stores authoritative
    L1 state absent

refined-owned parent:
    L0 stores zero
    L1 8x8 stores authoritative
```

Snow, surface water, soil water and groundwater are never represented as two independently advancing authoritative copies. Materialization/aggregation uses actual world-overlap area, including partial world-boundary cells.

Soil water preserves parent saturation under heterogeneous child capacity. The v0.7 area-weighted parent-capacity invariant makes this conservative by construction.

Mixed-resolution stepping validates inputs first, advances coarse and refined state in scratch storage and commits only after the whole routed day succeeds. The existing conservation regression remains green after v0.9 weather forcing was added.

### Weather truth

Static `ClimateSample` remains the long-run baseline. `WeatherState` owns only transient atmospheric state:

- temperature anomaly;
- moisture anomaly;
- exact integer day.

Spatial innovations are generated on a coarser synoptic lattice and interpolated to L0, then combined with autoregressive and neighbor memory. The state therefore avoids independent per-cell white noise without allocating an L1/L2 atmosphere.

Weather exposes precipitation, mean air temperature and PET through the forcing structures hydrology already consumed. Water ownership and persistence formats did not need to be rewritten for v0.9.

## 2. Adversarial findings and fixes made before v0.9 merge

### Finding A: storm intermittency changed the climate baseline

The first weather implementation produced coherent wet/dry behavior but only about 0.646 of the static climate precipitation over the 10-year regression fixture.

This was a real model error, not test noise. Wet-area frequency and temperature centering were already acceptable, so only the linear storm-intensity multiplier was calibrated. The original fixture then produced approximately 0.9997 of its climatological precipitation target.

The audit also found that one seed/domain was not enough evidence for a stochastic deterministic model. A second regression now runs 10 years on multiple world identities and partial/misaligned world bounds. Each world must remain bounded around its climate baseline and the aggregate must remain substantially closer to 1.

### Finding B: the new standalone weather/water helper could hide water-parameter drift

`ContinentalWaterState` is a historical v0.5 C++ type. Its state object does not own the `DynamicHydrologyParameters` used to construct it; the C ABI wrapper stores those parameters externally, while the raw C++ advance API accepts a parameter set on each call.

The first v0.9 coupled helper added a default `{}` parameter. That created a new silent failure mode: a continental state initialized with non-default bucket parameters could be weather-stepped with defaults by omission.

The v0.9 helper now requires an explicit parameter argument, and the CLI constructs one parameter object, uses it to initialize water and passes the same object to every coupled step. The authoritative multiresolution path does not have this weakness because `MultiresolutionWaterState` already owns its parameter set.

The older raw `ContinentalWaterState` API still permits callers to provide a different valid parameter set on later days. This is a pre-v0.9 design debt and is recorded below rather than silently claimed fixed.

### Finding C: Windows shared-library consumers were not actually tested

Before this audit CI covered Linux GCC, Linux Clang and Linux ASan/UBSan only. Yet the public C ABI has a Windows export branch and CMake advertises `BUILD_SHARED_LIBS`.

A Windows shared-library CI job was added. The first run immediately exposed a build-contract issue: setting `/WX` through `CMAKE_CXX_FLAGS` had removed exception-unwind semantics and MSVC rejected standard-library exception use. The target now explicitly requires `/EHsc`, and the Windows CI preserves it while treating warnings as errors.

For Windows shared builds, `WINDOWS_EXPORT_ALL_SYMBOLS` is enabled so public C++ consumers such as the CLI/tests can link the same shared target. The explicit `WORLDSIM_API` C ABI annotations remain unchanged.

The same gate then exposed two test-harness portability bugs that Linux semantics had hidden. The monolithic world regression tried to delete persistence fixtures while `std::ifstream` handles were still alive; Windows correctly rejected those deletions and the uncaught `filesystem_error` surfaced as CRT fast-fail `0xc0000409`. The read streams are now scoped to end before cleanup. Three C ABI persistence tests also hard-coded `/tmp`; they now use disposable relative paths in CTest's writable working directory. A final MSVC Release shared build runs the complete test suite without the former `/tmp` workaround.

### Finding D: coupled atomicity wording was stronger than the obvious implementation shape

The coupled weather helpers prepare weather, advance water, then perform a defensive `day_after` equality check before committing weather. Superficially that looks like an exception point after water mutation.

The audit followed both water callees. Under the supported contract this check is currently unreachable as a failure path:

- coupled stepping requires equal starting clocks;
- both water implementations reject `INT64_MAX` before mutation;
- both set `day_after = day_before + 1`;
- neither can legally return a different successful next day.

Therefore current rejected inputs cannot leave weather behind an already-advanced water state. The dedicated atomicity regression verifies byte-for-byte unchanged weather/water after an injected hydrology rejection.

The post-water defensive check remains a maintenance hazard if future water APIs change their clock semantics; a future unified simulation owner should eliminate this split-commit shape entirely.

## 3. Persistence audit

### What is currently strong

World persistence validates format/version, world configuration, patch bounds, normalized fields, duplicate coordinates, declared record count and trailing data. Patch serialization is canonicalized for reproducible byte ordering.

Multiresolution-water persistence validates world identity, exact day, raster dimensions, local soil capacities, ocean state, refined/coarse ownership exclusivity, refined clocks, topology alignment, duplicate parents, truncation and trailing data. Old uniform-capacity format v1 is explicitly rejected rather than reinterpreted under v2 semantics.

Weather persistence validates world identity, exact day, parameters, raster metadata, anomaly bounds, truncation and trailing data. Derived climate/elevation metadata is reconstructed instead of becoming a second serialized source of static truth.

### Strongest persistence limitation

The application now has three distinct persistence authorities:

1. `World::save()` for persistent L2 history;
2. multiresolution-water state;
3. weather state.

All are path-based writers and currently write directly to their target files with truncation. There is no single checkpoint generation containing all three states.

A process failure between saves can therefore leave individually valid files from different simulation days. Coupled APIs reject mismatched weather/water clocks after load, so the inconsistency is detectable, but there is no built-in complete previous generation to recover automatically.

This becomes more important before adding another persistent subsystem such as in-channel travel-time storage.

### Persistence portability limitation

Current binary formats write native POD representations. They are versioned and validated, but the project does not yet claim a portable cross-endian or bit-identical cross-platform save-file contract. That is distinct from logical deterministic evolution and should not be inferred from it.

## 4. API / ABI audit

The public C boundary remains opaque handles plus POD copy/report structures; v0.9 is additive and does not alter existing water POD layouts.

Weather has its own error channel and opaque handle. Coupled C weather + multiresolution water operates on the existing water handle rather than copying state.

The implementation currently repeats identical opaque-struct definitions across several C ABI translation units. This works only while those definitions remain ODR-equivalent and is a maintenance risk. A shared private ABI header would remove that duplication without changing the public C ABI.

The C++ `ContinentalWaterState` parameter-ownership debt described above remains the main semantic API inconsistency. `MultiresolutionWaterState` already has the stronger design.

## 5. Numerical and determinism audit

Checked numerical contracts include:

- finite/non-negative forcing validation;
- finite water stores and local soil-capacity limits;
- integer-day overflow rejection;
- bounded routed-discharge diagnostics before mutation;
- deterministic drainage ordering and tie-breaking;
- deterministic weather innovations from world identity/coordinate/day;
- exact clock equality at coupled boundaries;
- float storage with double conservation accounting.

The project still does **not** promise bit-identical floating-point evolution on every compiler/architecture. CI exercises GCC, Clang, sanitizers and now MSVC/shared builds, but that is compatibility coverage rather than a bitwise-determinism guarantee.

## 6. Scaling audit

The verified practical fixture is 449,208 L0 cells with 64 simultaneously refined water parents.

After v0.9 calibration, a GCC Release CI observation was approximately:

- weather state construction: 205 ms;
- 30 coupled weather + mixed-resolution water days: 3.87 s;
- mean coupled day: 129 ms;
- peak RSS: 136 MiB;
- maximum relative daily water-balance residual: about 5.9e-9.

These are observations, not performance guarantees.

The hard continental limit is 1,000,000 L0 cells. That maximum-size case has not been benchmarked or memory-profiled in CI and must not be described as verified merely because construction is guarded by the limit.

## 7. Remaining architectural debt after v0.9

Ordered by ability to invalidate future subsystem work:

1. **No unified simulation/checkpoint owner.** World L2 history, weather and water are separate objects/files. Their runtime clocks can be validated but their disk generation is not transactional.
2. **Standalone `ContinentalWaterState` does not own its construction parameters in C++.** The authoritative multiresolution path does; the legacy coarse-only path relies on callers/C wrapper discipline.
3. **Same-day channel routing has no persistent in-channel/travel-time storage.** Adding it would create another conserved dynamic authority and persistence burden.
4. **C ABI private handle layouts are repeated across translation units.** This is an implementation-maintenance hazard, not a public ABI break today.
5. **Weather is L0-only and PET is a simple temperature proxy.** No orographic downscaling, radiation, humidity or wind.
6. **Soil remains one vertical bucket and groundwater has no lateral aquifer state.**
7. **Persistence binary encoding is native POD.** Cross-endian/cross-platform file portability is not a current guarantee.

## 8. Selected next major slice: unified simulation state and checkpoint

The next dependency should **not** be channel travel time yet.

Channel travel time requires persistent conserved channel storage. Adding that now would introduce another dynamic authority before the existing World + Weather + Water authorities have one lifecycle/checkpoint boundary.

The selected v0.10 slice is therefore a unified `SimulationState` / checkpoint layer that:

- owns one `World`, its authoritative continental topology, `WeatherState` and `MultiresolutionWaterState` as one runnable simulation;
- exposes one exact global day invariant instead of asking application code to coordinate weather/water clocks;
- routes refinement/aggregation and daily stepping through that owner;
- writes one versioned compound checkpoint containing persistent world history, weather and water from the same generation;
- writes to a temporary file and publishes only a complete validated checkpoint rather than truncating the previous target first;
- validates section lengths/checksums, world identity, clocks and ownership before exposing a loaded state;
- provides an opaque C ABI handle and CLI run/resume path;
- includes future-equivalence, corruption/truncation, wrong-world/clock and Europe-scale regression/benchmark coverage.

This is a normal-sized subsystem slice, not a persistence cosmetic. Once it exists, channel travel-time state can be added inside a single simulation/checkpoint ownership model rather than creating a fourth independently coordinated save authority.
