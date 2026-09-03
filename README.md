# WorldSim v0.4.0 — dynamic water cycle

Headless C++20 simulation core for a large persistent world. v0.4 audits the v0.3 authoritative drainage boundary and adds the first time-dependent natural state: conserved snow, surface water, soil water, groundwater, evapotranspiration, runoff and baseflow on authoritative L1 tiles.

## Implemented

- Engine-independent C++20 core (`worldsim`).
- C ABI suitable for thin Unity/Godot/Unreal adapters.
- Spatial hierarchy:
  - L0 climate / continental drainage: 8192 m;
  - L1 regional terrain / refined drainage / dynamic water state: 1024 m;
  - L2 local persistent history: 64 m, 16×16 per L1 cell;
  - future entities use continuous coordinates.
- Europe-scale world bounds without eager L1/L2 allocation.
- Deterministic procedural terrain and climate scaffolding.
- Configurable sea-level datum.
- Whole-world authoritative L0 drainage and stable basin/outlet topology.
- Fixed 8×8 authoritative L1 refinement with stable cross-tile outlet/ingress edges.
- Dynamic hydrology state per active L1 cell:
  - snow water equivalent;
  - surface water;
  - soil-water bucket;
  - groundwater storage;
  - evapotranspiration;
  - infiltration/percolation;
  - quick runoff;
  - groundwater baseflow;
  - routed discharge through the authoritative drainage graph.
- Explicit upstream channel-volume injection for cross-tile coupling.
- Per-step water-balance report.
- Internal substeps of at most one day for longer advances.
- Deterministic smooth climatological forcing helper until a real weather layer exists.
- World identity embedded in authoritative tiles/state to prevent cross-world misuse.
- C ABI dynamic-hydrology handle, forcing, stepping and state-copy functions.
- CLI `watercycle` multi-day headless runner.
- Lazy persistent L2 materialization and `disturb_surface()`.
- Binary world save format remains v2 with v1 read compatibility.
- C++ tests and pure-C ABI tests.

## Core invariant

Dynamic hydrology does not choose its own drainage topology.

```text
whole-world L0 drainage
        ↓
authoritative 8×8 L1 topology
        ↓
weather/climate forcing
        ↓
snow ↔ surface → soil → groundwater
        ↓          ↓          ↓
      runoff     ET       baseflow
        └──────────┬───────────┘
                   ↓
       authoritative channel routing
```

For every advance:

```text
storage_before + precipitation + upstream_inflow
≈ storage_after + evapotranspiration + downstream_outflow
```

The residual is reported as `water_balance_error_m3` and tested.

## Scientific/model limitations

v0.4 establishes conserved dynamic state; it is not yet a complete catchment model.

- Terrain/climate are synthetic scaffolding, not reconstructed Europe.
- The included smooth climatological forcing is **not weather**. It exists so the hydrology layer can run before an atmosphere/weather system is built.
- Soil properties are currently generic bucket parameters, not spatially generated soil types.
- No lateral groundwater aquifer model, wetlands, floodplains, channel width/depth/velocity, erosion, sediment or vegetation feedback yet.
- L1 cross-tile topology is stable, but exact hydraulic water-surface continuity is not yet solved.
- Dynamic hydrology state is currently an explicit simulation state object/handle and is **not stored in `World::save()` yet**.
- There is no global simulation clock/scheduler yet; callers currently orchestrate multi-tile stepping and transfer upstream tile outflow into downstream ingress cells.

See `docs/DYNAMIC_HYDROLOGY.md`, `docs/CONTINENTAL_HYDROLOGY.md` and `docs/AUDIT_v0.3.md`.

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

Refine an L0 cell:

```bash
./build/worldsim_cli tile demo.ws 3 4 0.5
```

Run 365 days of the v0.4 water-cycle state with the temporary smooth climatological forcing provider:

```bash
./build/worldsim_cli watercycle demo.ws 3 4 365
```

The command reports cumulative precipitation, ET, outflow, initial/final storage, final mean water stores and maximum absolute daily balance error.

## Audits

- `docs/AUDIT_v0.1.md` — spatial/persistence foundation before v0.2.
- `docs/AUDIT_v0.2.md` — regional hydrology before v0.3.
- `docs/AUDIT_v0.3.md` — authoritative drainage boundary before v0.4.

## Next bounded milestone

Do **not** add erosion yet. The missing foundation is ownership and time orchestration of dynamic state:

1. global simulation clock;
2. active/lazy tile scheduler;
3. deterministic upstream→downstream multi-tile stepping;
4. persistence/catch-up of dynamic tile state;
5. then spatial soil properties and vegetation feedback can consume soil moisture and groundwater.
