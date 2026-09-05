# Audit v0.15

## Scope

First sparse persistent settlement/entity layer owned by `SimulationState`.

## Architecture decision

The read-only audit confirmed the requested boundary: settlement history is a separate runtime persistence authority rather than a `World` field. `World` remains the ecological/static authority and settlements consume it through derived diagnostics.

## Implemented

- sparse `SettlementState` with deterministic IDs;
- regional founding/query/list API;
- current-day terrain/water/vegetation/temperature/disturbance suitability;
- bounded population tendency toward environmental capacity;
- unified staged daily evolution;
- compound checkpoint v2 settlement section;
- explicit v1 -> empty-settlement migration;
- additive C ABI and v3 full-day report;
- focused C++ and C ABI regressions;
- Europe-scale unified benchmark extended with 64 settlements.

## Explicit non-scope

No migration, trade, roads, buildings, jobs, inventories, agriculture, wars, political entities, ownership maps, resource extraction, pathfinding, culture/religion or detailed demographics.

## Verification observation

On PR #21 head `3d624b80ebd04e1252841d1cee5a426c15eb0b04`, GitHub Actions run 189 completed green for GCC, Clang, ASan/UBSan and MSVC/shared. The GCC job also completed all existing Europe-scale gates.

Observed Europe-scale unified simulation checkpoint gate with 64 settlements:

- `benchmark_settlements=64`;
- `advance_5_days_ms=1146.814`;
- `checkpoint_save_ms=191.500`;
- `checkpoint_load_ms=968.444`;
- `checkpoint_bytes=22096248`;
- `peak_rss_kib=270744`;
- `max_relative_water_balance_error=5.886e-09`.

These are CI observations for that run, not fixed performance guarantees. A later docs/test-only head must still pass exact-head CI before merge.
