# Audit — v0.14 sparse persistent L2 vegetation

## 1. Audit entry state

The bounded cycle started from verified `main`:

```text
89ee9828bd083e3444dbf3bb5dfa4ebd1e20d926
```

That SHA contained merged v0.13 derived L1 atmospheric forcing and had a fully green post-merge GCC/Clang/sanitizer/MSVC matrix.

The read-only adversarial audit checked v0.13 refined forcing validation, dynamic-hydrology numerical staging, multiresolution-water v6 migration and compound checkpoint load boundaries.

No new confirmed correctness defect was found before feature selection.

One suspected forcing-preflight issue was rejected after inspection: refined child forcing is validated again inside the staged dynamic-hydrology step before any multiresolution state commit.

## 2. Selected bounded milestone

The next milestone is **sparse persistent L2 vegetation recovery**.

The selection follows existing architecture rather than adding another environmental subsystem arbitrarily:

- `RegionalSample` and `LocalCell` already expose deterministic `forest_potential`;
- `LocalCell` already persists surface `disturbance`;
- the existing disturbance primitive was explicitly intended to become an input to vegetation;
- L2 is already the sparse persistent local-history authority.

Therefore vegetation is added inside `World` local history rather than as another whole-world state object.

## 3. Authority

No `VegetationState` is introduced.

Authoritative state remains:

```text
SimulationState
├── World
│   └── sparse materialized L2 disturbance + vegetation biomass
├── WeatherState
└── MultiresolutionWaterState
```

Static `forest_potential` remains derived.

Dynamic `vegetation_biomass` and `disturbance` are persisted only for materialized L2 cells.

## 4. Model contract

For an in-world land L2 cell:

```text
D_next = D * exp(-1/730)
target = forest_potential * (1 - D_next)

T_factor = clamp((T + 2)/18, 0, 1)
M_factor = clamp((soil_saturation - 0.10)/0.60, 0, 1)
suitability = T_factor * M_factor

recovery = 1 - exp(-suitability/365)
B_next = B + (target - B) * recovery
```

Invariant:

```text
0 <= B <= forest_potential * (1 - D) <= 1
```

Disturbance commands immediately lower biomass to the new disturbed carrying capacity.

The constants are bounded simulation-scale heuristics, not empirical ecological calibration.

## 5. Environmental coupling

Each materialized regional patch derives one current-day forcing record.

Temperature is current L0 weather.

Soil saturation comes from authoritative water at the best already-owned resolution:

- exact L1 regional water when its climate parent is refined;
- L0 parent water otherwise.

No water or weather state is materialized by vegetation forcing.

## 6. Unified atomicity

The full simulation day stages sparse World vegetation before the existing atomic weather/water step.

Publication order:

```text
derive current vegetation forcing
stage World local history
advance staged vegetation
advance weather + water
swap staged local history (no-throw)
```

This prevents a rejected hydrology/weather step from leaving vegetation one generation ahead.

The old `advance_day()` remains source-compatible and uses the same underlying full generation.

## 7. Persistence migration

World persistence advances from v2 to **v3**.

v3 adds one biomass float to each materialized L2 cell.

v1/v2 migration preserves all old data and derives biomass only for local cells that both overlap configured world bounds and lie above sea level:

```text
forest_potential * (1 - disturbance)
```

All other migrated local cells receive zero biomass.

World v3 retains canonical patch ordering and strict corruption/trailing-data validation.

The compound simulation container is unchanged: vegetation lives inside its World section.

## 8. Public interfaces

C++ adds explicit sparse vegetation forcing, standalone World advance and `SimulationDayReport`.

The old weather/water day report remains available through the old `advance_day()` signature.

C ABI compatibility is additive. Existing structs are not expanded in place.

New vegetation PODs/functions expose:

- local biomass copies;
- explicit standalone forcing/advance;
- unified read-only local vegetation;
- `ws_simulation_advance_day_v2` with environment + vegetation diagnostics.

## 9. Regression evidence

Focused regressions verify:

- deterministic biomass initialization;
- immediate disturbance damage;
- warm/moist recovery;
- cold/dry stalled biomass;
- disturbance decay;
- exact forcing coverage;
- invalid forcing atomicity;
- no eager L2 materialization;
- canonical World v3 round trip;
- v2 → v3 local-history migration;
- unified day ownership;
- exact compound checkpoint vegetation state;
- exact deterministic future evolution after reload;
- C ABI capacity/error/query/advance behavior.

The first implementation head passed GCC warnings-as-errors, all CTest regressions and the existing Europe-scale gates; Clang and sanitizers also passed before benchmark/documentation finalization.

## 10. Europe-scale benchmark gate

The final benchmark workload intentionally uses real unified ownership rather than a standalone microbenchmark.

It contains:

- 449,208 L0 cells;
- 64 refined water parents;
- 64 corresponding materialized regional vegetation patches;
- 16,384 disturbed 64 m L2 cells;
- five unified warmup days;
- compound checkpoint save/load;
- exact equality of all channel cells and all 64 vegetation patches;
- one exact deterministic future day after reload.

Observed GCC Release values on the benchmark head:

- simulation construction: `671.042 ms`;
- materialize 64 refined parents: `10.747 ms`;
- five unified days: `739.681 ms`;
- checkpoint save: `386.885 ms`;
- checkpoint load: `751.586 ms`;
- checkpoint size: `22,093,640 bytes`;
- vegetation biomass-area after warmup: `12,098,057.493 m²`;
- vegetation disturbance-area after warmup: `33,325,391.497 m²`;
- peak RSS: `270,440 KiB`;
- maximum relative water-balance residual: `5.886e-9`.

These observations are not formal performance guarantees.

## 11. Deliberate limits

The feature is not a species/ecosystem simulator.

Deferred:

- species and age structure;
- succession and competition;
- dispersal;
- fire;
- nutrients/carbon;
- vegetation feedback into hydrology/erosion;
- grazing/agriculture.

## 12. Next decision boundary

With terrain, weather, water, persistent disturbance and sparse vegetation now available, the strongest product-level gap is a persistent entity/settlement layer able to consume those environmental signals.

The next milestone should therefore move up the stack rather than deepen hydrology or ecology without a concrete consumer.

## 13. Audit conclusion

v0.14 establishes a sparse persistent ecology-facing history at the correct existing authority boundary.

It does so without eager whole-world ecology allocation, without a new simulation clock, without a new checkpoint section, and without changing existing C ABI layouts.
