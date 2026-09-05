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

## Verification status

CI and benchmark observations are recorded in the PR/final milestone audit only after they complete. No benchmark timing or memory number is asserted here before observation.
