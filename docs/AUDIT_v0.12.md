# Audit — v0.12 reach-aware channel transport

## 1. Scope

v0.12 closes the uniform-residence limitation left by v0.11 without reopening dynamic-water ownership or adding a second channel authority.

The release combines two bounded changes:

1. deterministic per-reach channel transport metadata derived from authoritative continental topology;
2. a deliberately weak simulation-scale residence heuristic using D8 reach length, filled-elevation slope and accumulated discharge.

The global scheduler remains daily and preserves the established one-L0-edge-per-day causality.

## 2. Authoritative state and ownership

`MultiresolutionWaterState` remains the only dynamic-water authority.

Channel water is still one conserved L0 volume per terrestrial parent, independent of whether terrestrial bucket storage is coarse-owned or refined to L1.

Reach transport metadata is derived and read-only. It is reconstructed from authoritative topology at state creation/load and is not serialized as another persistence authority.

## 3. Current residence contract

For a terrestrial L0 reach:

```text
length_cells = 1 for cardinal D8, sqrt(2) for diagonal D8
slope        = max(downhill_gradient, 1e-5)
discharge    = max(accumulated_discharge_m3_s, 1)

residence_days = clamp(
    length_cells
    × (slope / 1e-5)^-0.08
    × (discharge / 100)^-0.06,
    0.75,
    3.0)

release_fraction = 1 - exp(-1 / residence_days)
```

The coefficients are simulation-scale heuristics, not empirical hydraulic constants.

Reach length remains dominant. Slope and discharge are intentionally weak modifiers because the daily scheduler cannot represent physical multi-reach sub-day celerity.

A flat cardinal reach at the 100 m3/s reference discharge retains a one-day residence.

## 4. Causality and conservation

Only channel storage present at the beginning of a global day may release during that day.

Current-day runoff, upstream arrivals and refined-tile outlet water become next-generation channel storage and cannot be re-released in the same global step.

Therefore one released parcel crosses at most one L0 edge per global day.

Whole-world conservation continues to include terrestrial coarse/refined storage plus all persistent L0 channel storage.

## 5. Public observability

C++ exposes derived per-reach transport properties through `channel_transport()`.

The standalone multiresolution-water C ABI and unified simulation C ABI expose equivalent read-only transport metadata.

The APIs remain mutation-controlled by the solver; arbitrary channel or transport setters are intentionally absent.

## 6. Persistence and migration

Multiresolution-water persistence is semantic format v5.

v5 keeps the authoritative byte layout introduced by v3. Transport metadata is not serialized.

Migration rules:

- v2: terrestrial/refined water is preserved and channel storage is initialized to zero because v2 had no persisted channel authority;
- v3: persisted water/channel storage is preserved exactly, then current v5 transport is derived from topology;
- v4: persisted water/channel storage is preserved exactly, then current v5 length/slope/discharge transport is derived from topology;
- v5: current semantics.

Format v1 remains rejected because it predates current spatial soil-capacity semantics.

## 7. Regression coverage

Permanent regressions cover:

- flat-cardinal one-day reference behavior;
- diagonal-length residence increase;
- weak discharge dependence;
- bounded steep-slope acceleration;
- actual channel-storage release matching exposed per-reach release fractions;
- one-edge/day routing under non-uniform release fractions;
- whole-world water conservation;
- rejection of non-finite filled elevation and accumulated discharge transport inputs;
- v3/v4 semantic migration to current transport;
- deterministic future evolution after migration;
- C++ and C ABI transport observability;
- Europe-scale multiresolution, weather and compound checkpoint gates.

## 8. Verified integration history

The reach-aware transport slice was merged in PR #16 after exact-head GCC, Clang, sanitizer and MSVC/shared CI passed, followed by a green push CI on merged `main`.

The bounded length/slope/discharge heuristic and v5 semantic migration were merged in PR #17 after the same exact-head matrix passed, including the Europe-scale GCC gates, followed by a green push CI on merged `main`.

The v0.12 release-hygiene change only aligns project/documentation versioning and this audit with those already-verified runtime changes.

## 9. Europe-scale observation

The current GCC Release fixture contains 449,208 L0 cells and 64 refined parents.

One audited run with the bounded residence heuristic observed:

- five unified days: about 790.442 ms;
- checkpoint size: 21,769,048 bytes;
- channel storage after five warmup days: 85,711,133,025.076 m3;
- maximum relative water-balance residual: 5.886e-9.

These are environment-specific observations, not API or performance guarantees.

## 10. Deliberate limitations

v0.12 does not claim:

- empirically calibrated river celerity;
- sub-daily or multi-L0-edge flood-wave propagation;
- independent hydrograph lag/attenuation parameters;
- channel width/depth/capacity or Manning roughness;
- backwater or floodplain hydraulics.

Further river-routing work should wait for a concrete requirement that exceeds the daily one-edge model.

## 11. Selected next bounded slice

The next strongest resolution mismatch is atmospheric forcing inside refined terrestrial water.

Today every active 1024 m L1 child of a refined 8192 m L0 parent receives the same parent L0 precipitation, temperature and PET forcing.

The next bounded milestone should add **stateless derived L1 atmospheric forcing** while keeping `WeatherState` L0-authoritative.

Required constraints:

- no new persistent L1 atmospheric state;
- derive child temperature from parent forcing plus bounded local-elevation correction;
- derive a weak deterministic terrain/orographic precipitation redistribution;
- preserve parent precipitation volume exactly across active L1 child overlap area;
- derive PET from the corrected child temperature through the existing forcing approximation;
- coarse-owned L0 water remains unchanged;
- rejected forcing/downscaling input must not split simulation clocks or partially mutate water;
- C++/C ABI observability should be additive and read-only;
- Europe-scale benchmark coverage must remain in the existing CI gate.

Pressure, wind, humidity, cloud/radiation physics and numerical weather prediction remain out of scope.

## 12. Audit conclusion

v0.12 is a sufficient channel-routing model for the current daily world scale. The transport boundary is conserved, deterministic, persistent and reach-aware without implying unsupported hydraulic precision.

The next useful improvement is therefore not another channel coefficient. It is resolving the forcing mismatch between L0 atmosphere and selectively authoritative L1 terrestrial hydrology without creating a duplicate atmospheric authority.
