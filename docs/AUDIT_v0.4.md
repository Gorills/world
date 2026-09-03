# Audit of v0.4.0 before global time orchestration

## Scope

The published v0.4 baseline was reviewed as the dependency for the next world-scale dynamic layer:

- C++/C public contracts;
- authoritative L0 and refined L1 drainage ownership;
- dynamic L1 water-state lifetime and initialization;
- multi-tile handoff semantics;
- memory scaling at Europe size;
- persistence/time ownership;
- build/test coverage required for Git-based development.

## Verified baseline

The published source layout was reconstructed locally and built in Release with warnings promoted to errors. Both the C++ and pure-C ABI test suites pass. The large C++ test had to be stored in three include fragments during the initial connector import; concatenating those fragments is byte-identical to the original v0.4 test source.

## Main architectural finding

### An L1-only global scheduler would violate the project's multiresolution truth model

v0.4 dynamic state exists only after an authoritative 8x8 L1 tile is materialized. `make_dynamic_hydrology_tile_state()` initializes that tile from fixed initial soil/groundwater parameters. There is no older coarse dynamic state from which a tile can recover the water history that occurred before materialization.

Therefore a scheduler that merely advances currently materialized L1 tiles has two bad choices:

1. leave distant tiles unadvanced, so materializing one later resets its hydrological history; or
2. materialize and advance every L1 tile in the world from day zero.

For the Europe-scale fixture used by the project, 449,208 L0 cells imply 28,749,312 L1 cells. On the current Linux/GCC ABI `sizeof(DynamicHydrologyCellState) == 56`, so eagerly storing only those L1 cell states would consume about 1.61 GB (about 1.50 GiB), before topology, vectors, allocators, weather, soil, vegetation, entities, or any engine-side data.

This is the strongest failure mode for the previously proposed "global clock + L1 tile registry" milestone.

## Corrected next-layer decision

Do not build the world scheduler directly on L1 state.

The next bounded layer must establish a cheap, authoritative time-dependent L0 water state for every continental cell:

```text
Simulation day
    ↓
all L0 land cells
    ↓
snow / surface / soil / groundwater
    ↓
local runoff + baseflow
    ↓
authoritative L0 drainage DAG
    ↓
terminal ocean/world-boundary outflow
```

This gives every part of the world a hydrological history at low cost. A later L1 materialization can then refine a parent L0 state conservatively instead of inventing initial conditions at the moment a player arrives.

## Required invariants for the L0 dynamic layer

- one exact global simulation day index, not independently drifting per-tile clocks;
- deterministic daily stepping;
- non-negative stores;
- whole-continent water balance;
- routing only along the existing authoritative L0 drainage graph;
- state alignment/identity checks against the world/topology that created it;
- memory proportional to L0 cells, not all L1 cells;
- no L2 materialization as a side effect;
- forcing remains an input/provider boundary rather than being hidden inside hydrology.

## Findings not treated as blockers

- v0.4's L1 handoff contract is correct for explicitly orchestrated neighboring tiles; it is not itself the scheduler.
- Dynamic L1 state is not persisted yet; that is documented and should be solved together with multiresolution state ownership, not by embedding arbitrary L1 handles in `World::save()`.
- The current bucket parameters and smooth climatological forcing remain scaffolding.
- Hydraulic travel time, flood stage, channel geometry, lateral groundwater, erosion, and vegetation feedback remain later layers.
- The terrain generator contains a stale comment saying sea level is not modeled; the terrain field is in fact only a synthetic elevation datum while sea level is applied by continental hydrology. This is documentation debt, not a runtime defect.

## Git process finding

The repository had no CI because v0.4 predated Git publication. Since subsequent work will be PR-driven, the audit adds a minimal GitHub Actions matrix for GCC/Clang Release builds with warnings-as-errors plus an ASan/UBSan job. This is directly part of the new merge contract, not simulation functionality.

## Result

No v0.4 runtime bug was found that invalidates the existing dynamic-tile solver. The planned next architecture was changed before implementation: v0.5 should be an authoritative continental dynamic-water state plus global daily clock. L0↔L1 conservative refinement and lazy detailed-tile scheduling should follow after that state exists.
