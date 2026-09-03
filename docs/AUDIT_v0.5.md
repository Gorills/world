# Audit of v0.5 before multiresolution water ownership

## Scope

The merged v0.5 continental-water layer was reviewed before connecting L0 history to sparse L1 detail. The review covered:

- public C++ and C ABI contracts;
- continental topology/state identity;
- daily clock and routing order;
- water conservation and ocean semantics;
- invalid-input atomicity and numeric bounds;
- indexing and partial world cells;
- persistence assumptions;
- CI and Europe-scale memory direction.

## Baseline result

The v0.5 architecture remains the correct coarse history layer. A global L1 allocation is still rejected: the project fixture contains 449,208 L0 cells, while eager detailed state would require tens of millions of L1 cells.

The next layer therefore remains sparse conservative refinement rather than replacing the L0 state.

## Runtime defects found

### 1. Finite float inputs could overflow persistent water state

The v0.5 forcing/parameter validation originally accepted arbitrary finite non-negative `float` values. Formally finite values near `FLT_MAX` could overflow subsequent persistent float stores or routed-discharge diagnostics.

The audit fix adds a deliberately non-physical numerical safety envelope and performs the checks before mutation. Regression coverage verifies rejected extreme finite forcing leaves the global day and continental state byte-for-byte unchanged.

This was fixed and merged separately before the v0.6 ownership layer.

### 2. Extreme coordinates could overflow `ContinentalWaterState::index_of()`

A later v0.6 C ABI sanitizer test called the existing public L0 index function with `INT64_MAX`. With a negative raster minimum, signed subtraction overflowed before the function could return its intended `out_of_range` error.

The fix computes the already-known-nonnegative coordinate delta in unsigned arithmetic after the lower-bound check. A dedicated regression covers both `INT64_MAX` and `INT64_MIN` under UBSan and preserves valid coordinate/index round trips.

This was also fixed and merged separately before the v0.6 feature merge.

## Ownership decision for v0.6

The scheduler must have one independent water truth per parent region:

- **coarse-owned**: L0 stores are authoritative and no detailed tile exists;
- **refined-owned**: the parent L0 stores are zero and the sparse L1 tile owns the conserved stores.

A non-zero aggregate L0 mirror was rejected for this milestone because the existing L0 solver would make it too easy to step the mirror independently and double-count water. A zero parent store makes ownership explicit and testable.

## Refinement decision

The current hydrology parameters provide one global soil capacity rather than spatial child capacities. Therefore v0.6 uses the parent depth uniformly over terrestrial children and proves conservation through actual overlap areas.

Introducing terrain-derived or synthetic spatial soil capacity only to make refinement heterogeneous would be premature scope expansion. When a real spatial soil layer exists, it can replace this distribution rule while preserving the same volume contract.

## Persistence decision

Persistence is part of the bounded ownership milestone. Without it, reload would discard which parents are L1-owned and restore a contradictory coarse-only truth.

Dynamic water remains an explicit simulation object, so v0.6 adds a separate versioned multiresolution-water file instead of changing the existing `World::save()` v1/v2 format. Derived topology is reconstructed from world/topology input on load.

## Result

After the two audit fixes, no remaining v0.5 defect was found that requires changing the coarse hydrology equations. The bounded v0.6 work is therefore ownership, conservative transfer, coupled routing, persistence and C ABI integration rather than a rewrite of the v0.5 bucket model.
