# Audit of v0.1 spatial foundation

Scope: inspect the actual v0.1 source, public C++/C contracts, coordinate rules, persistence and tests before relying on the foundation for hydrology.

## Baseline verification

The untouched v0.1 source was copied into a clean v0.2 work tree, configured with CMake/Ninja, built in Release and both existing test binaries passed before changes were made.

## Findings that changed code

### 1. Zero disturbance materialized persistent detail

`disturb_surface(..., amount = 0)` materialized intersecting L2 patches even though no state could change.

Fix: zero amount returns `0` before clipping/materialization.

### 2. Disturbance count did not mean changed state

The function counted intersecting cells even when their existing disturbance was already at least the requested value.

Fix: the return value now counts cells whose persistent disturbance actually increased.

### 3. Mutable materialized state bypassed simulation commands

The public C++ API returned `LocalPatch&`. A caller could directly edit disturbance, generated fields or even `regional_coord`, bypassing validation and breaking the map-key/persistence invariant.

Fix: the public materialization API returns `const LocalPatch&`. Internal mutation uses a private mutable materializer and explicit commands.

### 4. Coordinate validation was not precision-safe

The old extreme-bound check tried to approach the formal `int64_t` grid limit using `double`. At those magnitudes, adjacent 1 km cells are no longer representable as distinct doubles, so the formal integer range was not a meaningful safe range.

Fix: world bounds are limited by the precision required by the 64 m L2 hierarchy (still astronomically larger than a planetary world), and direct coordinate helpers reject non-finite/out-of-range input.

### 5. Public hierarchy helpers accepted invalid configuration

Direct coordinate helper calls could be used with invalid cell sizes/ratios without first calling `WorldConfig::validate()`.

Fix: helpers validate the preconditions they rely on.

### 6. L0 climate queries ignored configured world bounds

`sample_region()` respected the configured world, while direct `sample_climate()` calls could sample outside it.

Fix: public L0 queries now enforce world bounds consistently.

### 7. Persistence accepted malformed duplicate/out-of-bounds state

The v0.1 loader could silently collapse duplicate patch coordinates and did not validate all loaded normalized values.

Fix: reject duplicate/out-of-bounds patches, non-finite values and normalized values outside `[0,1]`.

### 8. Persistence allocation defense was too coarse

A malicious/corrupt declared patch count could imply a very large allocation before proving enough bytes existed in the file.

Fix: declared records are checked against remaining file bytes before reserve/read.

### 9. Persistence could silently accept extra bytes

Version 1 has no optional tail sections, but trailing bytes were ignored.

Fix: v1 loading requires exact end-of-file after the declared records.

### 10. Save serialization trusted a mutable patch coordinate

Canonical serialization sorted/wrote `LocalPatch::regional_coord`, even though the owning map key was the actual storage identity.

Fix: direct external mutation is removed; saving additionally verifies key/value coordinate consistency and serializes the map key.

### 11. Pointer-returning C API functions did not contain unknown exceptions

Integer-returning guarded calls had a catch-all; create/load paths did not.

Fix: pointer-returning C API paths also return null and store a generic error for unknown exceptions.

### 12. Regional elevation was mislabeled as a mean

The L1 terrain field was named `mean_elevation_m`, but the implementation samples the coarse DEM at the regional cell center; it does not integrate an area mean.

Fix: rename the field to `elevation_m`. The C struct member keeps the same type/order, so its binary layout is unchanged; source code using the old member name must update.

## Findings intentionally not changed

- Binary save v1 is native-endian. Cross-endian portability is not yet promised.
- Procedural noise uses floating-point/libm; tested-platform determinism is verified, universal bit-identical cross-architecture determinism is not.
- L1 static fields are coarse samples/proxies, not guaranteed aggregates of generated L2 values. This becomes a required invariant when dynamic environmental state is introduced.
- Synthetic climate/terrain remain scaffolding. Hydrology can validate algorithms on them, but they are not a claim of real European geography.

## Regression coverage added

Tests now cover the changed contracts above, including zero/no-change disturbance, const materialized state, coordinate/hierarchy validation, L0 bounds, malformed duplicate/trailing save data and prior canonical save/load behavior.
