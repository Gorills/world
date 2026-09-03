# Architecture audit — v0.11

This audit reviews WorldSim after persistent channel transport was added to the unified v0.10 runtime/checkpoint architecture.

## 1. Scope reviewed

The audit covers the v0.11 changes and their affected contracts:

- authoritative `MultiresolutionWaterState` ownership;
- daily coarse/refined routing order and mass conservation;
- refined L0/L1 ingress/outlet boundaries;
- invalid-step atomicity;
- multiresolution-water persistence and migration;
- compound `SimulationState` checkpoint behavior;
- additive C ABI observability;
- Linux GCC/Clang, ASan/UBSan and Windows shared-library consumers;
- permanent Europe-scale water/weather/simulation gates.

Terrain, climate, weather generation, soil bucket equations, terrestrial refinement formulas and drainage topology generation are intentionally outside the v0.11 behavior change.

## 2. Conserved ownership

v0.11 keeps one water authority rather than creating a fourth simulation component:

```text
SimulationState
    World
    derived continental topology
    WeatherState
    MultiresolutionWaterState
        terrestrial L0/L1 stores
        L0 channel storage
```

Every terrestrial L0 cell has one persistent non-negative channel volume. This channel volume remains L0-owned even when the parent's terrestrial bucket ownership is refined to an 8×8 L1 tile.

Materialization/aggregation therefore conserve and transfer snow, surface water, soil water and groundwater as before, while leaving the parent channel volume unchanged. This avoids creating a second channel representation solely because terrestrial bucket resolution changed.

Ocean L0 cells are required to hold zero channel water.

## 3. Finite daily transport

Before v0.11, quick runoff/baseflow could traverse the L0 drainage DAG within one daily call. The travel time across an arbitrary number of L0 cells was therefore effectively zero at the simulation time scale.

v0.11 introduces one fixed linear reservoir per L0 cell:

```text
release = start_of_day_channel_storage × 0.6321205588285577
```

Only storage present at the beginning of the day contributes to that release. Current-day runoff, upstream arrival and refined-tile outlet water are next-generation channel storage and cannot be re-released during the same global step.

This establishes a structural invariant stronger than merely reducing discharge:

```text
one channel parcel crosses at most one L0 edge per global day
```

The coefficient is the one-day response of a one-day e-folding reservoir. It is deliberately global and fixed in v0.11.

## 4. Refined boundary

When an L0 release enters a refined downstream parent, the existing deterministic tile-connection rule selects the exact L1 ingress child.

The external inflow then traverses the authoritative 8×8 L1 drainage graph. The detailed solver's external outlet is added to that refined parent's L0 channel store for the next day.

It is not forwarded through the parent's L0 downstream edge in the same day. Current-day refined runoff follows the same outlet-to-parent-channel rule.

This keeps the L1 drainage graph physically relevant without allowing a refined tile to bypass the new L0 travel-time boundary.

## 5. Conservation and the development-time overwrite defect

Whole-world water storage now includes channel water:

```text
coarse-owned terrestrial stores
+ refined-owned terrestrial stores
+ L0 channel storage
```

The external balance remains precipitation in, ET and terminal release out.

The first v0.11 scheduler implementation exposed a concrete failure mode during CI. `channel_next` began as the old channel array. An upstream release could be added to a downstream scratch cell, but when that downstream cell was later processed the code assigned its own residual with:

```text
channel_next[downstream] = old_downstream - release
```

That assignment erased the already accumulated upstream arrival. The existing 60-day weather-driven mixed-resolution conservation regression failed on GCC/Clang/sanitizers and prevented release.

The corrected scheduler computes release only from immutable start-of-day state and subtracts that release from the scratch next-state value:

```text
channel_next[cell] -= release
```

Any upstream arrivals already accumulated in the scratch value are preserved, while they still cannot affect the current day's release calculation.

After this fix, the same conservation regression and the Europe-scale gates pass again. This is a materially useful regression: it demonstrably detected the exact bug being fixed rather than merely checking that the new code compiles.

## 6. Atomicity

Daily stepping validates forcing, component clocks, terrestrial stores, soil capacities, channel-array shape, channel finiteness/non-negativity and numerical bounds before publication.

The next coarse terrestrial cells, refined child cells and channel array live in scratch state. The authoritative state and global day are updated only after the complete calculation succeeds.

The invalid-input regression snapshots the channel array together with coarse/refined state and requires all of it to remain unchanged after rejection.

## 7. Persistence migration

Multiresolution-water persistence advances from v2 to **v3** because the authoritative conserved state changed.

v3 stores one `double` channel volume per L0 cell in addition to the existing world identity, hydrology parameters, day, coarse state and sparse refined ownership.

The loader validates channel shape, finite/non-negative values and zero channel water for ocean cells.

v2 has a well-defined migration because it had no persistent in-channel authority: a valid v2 file loads with its existing terrestrial/refined state and a zero-initialized channel array.

v1 remains rejected. Its incompatibility is unrelated to channels: it predates v0.8 local soil-capacity semantics and cannot be silently reinterpreted.

The persistence regression includes exact v3 round-trip, malformed channel rejection and an explicit synthetic v2 migration fixture rather than only testing the new writer against the new reader.

## 8. Compound checkpoint authority

The `SimulationState` compound container remains exactly three authoritative sections:

```text
World
Weather
Multiresolution Water
```

Channel state lives inside the Multiresolution Water section. This matches the v0.10 audit requirement and avoids a separate channel save generation or another component clock.

The Europe simulation benchmark now requires:

- non-zero channel storage before checkpointing;
- exact channel equality across every one of the 449,208 L0 cells after reload;
- exact channel equality again after advancing both the original and reloaded simulations one future day;
- the existing exact sampled weather/refined-state and step-report equivalence.

## 9. C ABI

v0.11 does not modify existing public C POD layouts or existing function signatures.

It adds read-only query functions for:

- one L0 channel volume;
- total channel storage;

on both the standalone `ws_multiresolution_water_state` handle and the unified `ws_simulation_state` handle.

The C API exposes no arbitrary channel setter. Mutation remains owned by the simulation solver.

C regressions verify initial zero state, non-zero state after stepping, exact standalone persistence, exact compound checkpoint preservation, future channel equivalence and out-of-range error behavior.

## 10. Scale and portability gates

A functional v0.11 head was run through the permanent CI matrix before release-document changes:

- GCC Release, warnings as errors: pass;
- Clang Release, warnings as errors: pass;
- ASan/UBSan: pass;
- Windows MSVC shared-library consumers: pass;
- Europe multiresolution benchmark: pass;
- Europe weather benchmark: pass;
- Europe compound simulation checkpoint benchmark: pass.

One GCC Release Europe checkpoint observation on 449,208 L0 cells and 64 refined parents measured:

- simulation construction: `815.272 ms`;
- materialize 64 refined parents: `14.048 ms`;
- five unified days: `776.130 ms`;
- checkpoint save: `170.951 ms`;
- checkpoint load including topology reconstruction: `939.440 ms`;
- checkpoint size: `21,769,048 bytes` (~20.76 MiB);
- channel storage after five warmup days: `85,772,959,568.875 m³`;
- peak RSS: `238,800 KiB`;
- maximum relative water-balance residual: `5.886e-9`.

These numbers are environment-specific observations, not performance guarantees.

The checkpoint-size increase is expected: v3 adds one 8-byte channel value for each L0 cell, plus existing container overhead.

## 11. Remaining architectural debt

The release does not remove the broader limitations recorded in earlier audits:

1. **Channel transport uses one global fixed reservoir coefficient.** It now has conserved finite travel time, but residence time is not derived from reach length, slope, discharge, width/depth or velocity.
2. **No floodplain/wetland exchange or channel capacity.** Channel storage cannot overtop, inundate or interact with a floodplain representation.
3. **POSIX checkpoint publication still lacks parent-directory fsync.** Atomic replacement is present, but strongest power-loss directory-entry durability is not claimed.
4. **Persistence remains native-POD.** Cross-endian/fully ABI-neutral save portability is not guaranteed.
5. **Weather remains L0-only.** Refined water receives parent L0 atmosphere without sub-grid/orographic weather.
6. **Groundwater has no lateral aquifer state and soil remains one vertically aggregated bucket.**
7. **FNV checksums are non-cryptographic accidental-corruption checks.**

None of these is a release blocker for the bounded v0.11 contract because they are not represented as guarantees by the implementation or documentation.

## 12. Selected next major slice

The next hydrologic dependency should build on the new conserved channel ownership rather than replace it with a parallel model.

The smallest materially stronger slice is **derived per-reach transport time**:

- derive a deterministic L0 reach residence-time parameter from already authoritative reach/topology/world fields;
- replace the single global release coefficient with a validated per-L0 coefficient while preserving the same start-of-day release rule;
- retain one-edge-per-day causality unless a deliberate sub-daily scheduler is introduced;
- keep channel volume in `MultiresolutionWaterState` and format migrations explicit;
- add long-path impulse and steady-flow regressions that distinguish reach-dependent travel time;
- avoid adding floodplain inundation or full Saint-Venant hydraulics in the same bounded slice.

This changes the most important remaining simplification in v0.11 without reopening runtime ownership or persistence architecture.

## 13. Audit conclusion

v0.11 closes the strongest hydrologic omission selected by the v0.10 audit. Channel water is now explicit conserved state, has finite daily travel time, respects refined boundaries, participates in whole-world mass balance, survives standalone and compound persistence, and is observable through additive ABI queries.

The CI-discovered downstream-arrival overwrite was fixed at the scheduler cause rather than hidden by tolerance changes. No release-blocking ownership, conservation, persistence or ABI defect is known in the audited v0.11 scope. Merge is gated on the complete permanent CI matrix for the exact final release head.
