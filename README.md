# WorldSim v0.5.0 — continental dynamic water state

Headless C++20 simulation core for a large persistent world. v0.5 adds the first world-scale time-dependent state: one exact global day and conserved coarse water stores for every authoritative L0 cell. This fixes the v0.4 architectural gap where a lazily materialized L1 tile had no hydrological history before materialization.

## Implemented

- Engine-independent C++20 core (`worldsim`).
- C ABI suitable for thin Unity/Godot/Unreal adapters.
- Spatial hierarchy:
  - L0 climate / continental drainage / coarse dynamic water: 8192 m;
  - L1 regional terrain / refined drainage / detailed dynamic water: 1024 m;
  - L2 local persistent history: 64 m, 16×16 per L1 cell;
  - future entities use continuous coordinates.
- Europe-scale world bounds without eager L1/L2 allocation.
- Deterministic procedural terrain and climate scaffolding.
- Configurable sea-level datum.
- Whole-world authoritative L0 drainage and stable basin/outlet topology.
- Fixed 8×8 authoritative L1 refinement with stable cross-tile outlet/ingress edges.
- v0.4 detailed L1 dynamic hydrology remains available for explicitly orchestrated tiles.
- v0.5 authoritative continental water state for every L0 cell:
  - one exact integer simulation day;
  - snow water equivalent;
  - surface-water store;
  - soil-water bucket;
  - groundwater store;
  - evapotranspiration;
  - infiltration/percolation;
  - quick runoff;
  - baseflow;
  - same-day routing through the immutable continental drainage DAG;
  - whole-continent water-balance report.
- Deterministic smooth daily forcing helper until a weather system exists.
- Atmospheric forcing over ocean cells is accepted but excluded from terrestrial stores/balance.
- Full forcing pre-validation: rejected daily steps are atomic.
- C ABI handle, forcing, stepping and state-copy functions for continental water.
- CLI `continental-water` world-scale headless runner.
- Lazy persistent L2 materialization and `disturb_surface()`.
- Binary world save format remains v2 with v1 read compatibility.
- PR CI: GCC/Clang warnings-as-errors plus ASan/UBSan.

## Multiresolution truth

v0.5 deliberately does **not** make every 1 km L1 cell active for the whole world.

```text
one global simulation day
        ↓
all authoritative L0 cells (~8 km)
        ↓
coarse snow / surface / soil / groundwater history
        ↓
continental drainage DAG
        ↓
terminal ocean/world-boundary outflow

selected places later
        ↓
conservative L0 → L1 refinement
        ↓
detailed local hydrology
```

For the project Europe-scale fixture, storing current `DynamicHydrologyCellState` for every L1 cell would be about 1.50 GiB before other systems. The coarse L0 state avoids that while preserving world history.

## Water-balance invariant

For every continental day:

```text
storage_before + terrestrial_precipitation
≈ storage_after + terrestrial_ET + terminal_outflow
```

The residual is reported as `water_balance_error_m3`. Runoff/baseflow are routed only through the precomputed authoritative L0 drainage DAG.

## Scientific/model limitations

v0.5 establishes world-scale temporal ownership, not a complete physical catchment model.

- Terrain/climate are synthetic scaffolding, not reconstructed Europe.
- Smooth forcing is **not weather**; a later WeatherSystem will replace it.
- Soil properties are generic global bucket parameters, not spatial soil types.
- L0 routing currently moves daily quickflow/baseflow through the DAG within the same daily step; channel travel time and flood-wave hydraulics are not modeled yet.
- No lateral groundwater aquifers, wetlands, floodplains, channel geometry, erosion, sediment or vegetation feedback yet.
- Detailed L1 state is not yet conservatively materialized from / aggregated back into the L0 parent state.
- Continental dynamic state is not yet persisted in `World::save()`; save format remains unchanged in v0.5.

See `docs/CONTINENTAL_WATER.md`, `docs/AUDIT_v0.4.md`, `docs/DYNAMIC_HYDROLOGY.md` and `docs/CONTINENTAL_HYDROLOGY.md`.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Shared library:

```bash
cmake -S . -B build-shared -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build build-shared
```

## CLI

Create a demo save:

```bash
./build/worldsim_cli demo demo.ws
```

Authoritative whole-world drainage:

```bash
./build/worldsim_cli continent demo.ws 25
```

Run the v0.5 world-scale coarse water history:

```bash
./build/worldsim_cli continental-water demo.ws 365
```

Run detailed v0.4 L1 water state for one authoritative tile:

```bash
./build/worldsim_cli watercycle demo.ws 3 4 365
```

## Audits

- `docs/AUDIT_v0.1.md` — spatial/persistence foundation before v0.2.
- `docs/AUDIT_v0.2.md` — regional hydrology before v0.3.
- `docs/AUDIT_v0.3.md` — authoritative drainage boundary before v0.4.
- `docs/AUDIT_v0.4.md` — rejects an L1-only scheduler and establishes the v0.5 L0 state boundary.

## Next bounded milestone

Do not add vegetation or erosion yet. The next required boundary is **conservative L0↔L1 state refinement**:

1. materialize an authoritative L1 tile from its parent L0 water stores without creating/destroying water;
2. advance detailed L1 state while preserving one global day;
3. aggregate L1 back into L0 when detail is released;
4. define persistence for the authoritative coarse state and detailed overrides;
5. only then allow soil/vegetation systems to depend on long-lived local moisture history.
