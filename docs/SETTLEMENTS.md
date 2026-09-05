# Settlements

v0.15 introduces the first persistent gameplay entity authority.

## Persistent state

```cpp
Settlement {
    SettlementId id;
    CellCoord regional_coord;
    double population;
    std::int64_t founded_day;
}
```

Only founded settlements are stored. IDs are deterministic, monotonic and never recomputed from environment. Duplicate regional ownership is rejected.

## Environment consumption

Suitability is derived on demand from existing authorities:

- regional terrain;
- current weather temperature;
- authoritative current soil-water state (refined regional state when present, otherwise its L0 parent);
- persistent local vegetation biomass when materialized;
- persistent local disturbance.

For a non-materialized regional patch, the read-only diagnostic uses static forest potential as the unmaterialized vegetation baseline and zero persistent disturbance. It does not create L1/L2 state. Founding materializes that regional local patch, after which persistent biomass/disturbance are consumed directly.

The v0.15 factors and base capacity are simulation-scale heuristics with explicit clamps. They are not empirical demographic calibration.

## Daily evolution

Population has only a bounded tendency toward current environmental capacity. The update is intentionally slow, finite and non-negative. The milestone contains no migration, trade, roads, buildings, jobs, inventory, agriculture, politics, warfare, pathfinding, detailed demographics or settlement interaction.

Settlement next-state is staged before coupled weather/water advancement. It commits only after the environment step succeeds, using a no-throw state swap.

## Persistence

Compound simulation checkpoints are format v2 and contain World, Weather, Water and Settlement sections. The settlement payload validates counts, IDs, coordinates, population, founded day and trailing data. Pre-settlement format-v1 compound checkpoints migrate to an empty `SettlementState`.

Derived suitability is never serialized.

## C ABI

The additive C ABI provides settlement POD/query/list/founding/suitability functions and `ws_simulation_advance_day_v3`. Earlier report layouts and functions remain unchanged.
