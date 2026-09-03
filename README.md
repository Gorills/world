# WorldSim v0.6.0 — conservative multiresolution water ownership

Headless C++20 simulation core for a large persistent world. v0.6 connects the authoritative world-scale L0 water history to sparse detailed L1 tiles without creating two independent truths for the same region.

## Implemented

- Engine-independent C++20 core (`worldsim`).
- C ABI suitable for thin Unity/Godot/Unreal adapters.
- Spatial hierarchy:
  - L0 climate / continental drainage / coarse dynamic water: 8192 m;
  - L1 regional terrain / authoritative refined drainage / selective detailed dynamic water: 1024 m;
  - L2 local persistent history: 64 m, 16×16 per L1 cell;
  - future entities use continuous coordinates.
- Europe-scale world bounds without eager L1/L2 allocation.
- Deterministic procedural terrain and climate scaffolding.
- Configurable sea-level datum.
- Whole-world authoritative L0 drainage and stable basin/outlet topology.
- Fixed 8×8 authoritative L1 refinement with stable cross-tile outlet/ingress edges.
- Authoritative L0 dynamic water history for every world cell:
  - exact integer global day;
  - snow, surface water, soil water and groundwater;
  - rain/snow, melt, infiltration, ET, percolation, baseflow and runoff;
  - deterministic routing through the continental drainage DAG;
  - whole-world water-balance report.
- Conservative multiresolution ownership:
  - unrefined parent: L0 stores are authoritative;
  - refined parent: L0 stores are zero and sparse 8×8 L1 state is authoritative;
  - actual overlap-area conservation for partial world cells;
  - conservative L1→L0 aggregation;
  - repeated materialize/dematerialize;
  - one exact clock across both ownership modes.
- Coupled mixed-resolution daily routing:
  - coarse upstream → exact refined ingress;
  - refined outlet → coarse downstream;
  - refined-to-refined transfer through deterministic boundary cells;
  - no independent coarse step for a refined parent.
- Atomic rejected mixed-resolution steps.
- Separate versioned persistence for dynamic multiresolution water ownership; existing `World::save()` remains v2 with v1 read compatibility.
- C ABI for multiresolution ownership, stepping, state copy and persistence.
- Deterministic smooth forcing helpers until a weather system exists.
- Lazy persistent L2 materialization and `disturb_surface()`.
- PR CI: GCC/Clang warnings-as-errors plus ASan/UBSan.
- Europe-scale mixed-resolution benchmark executable.

## Ownership invariant

The same water volume is never independently owned by both L0 and L1.

```text
unrefined parent
    L0 stores = authoritative
    L1 tile   = absent

refined parent
    L0 stores = zero
    L1 tile   = authoritative
```

This gives the world one state with variable resolution rather than separate coarse and detailed simulations that can drift.

## Mixed-resolution routing

```text
coarse upstream
      ↓
refined ingress child
      ↓
8×8 authoritative L1 routing
      ↓
refined outlet
      ↓
coarse downstream
```

The continental topological order determines when each parent is processed. Upstream channel volume reaches a refined ingress before that refined parent is stepped, and its outlet is forwarded exactly once.

## Conservation

For materialization and aggregation, each conserved store is transferred by volume using actual in-world cell overlap areas. This includes partial L0/L1 cells along configured world bounds.

For each global mixed-resolution day:

```text
storage_before + terrestrial_precipitation
≈ storage_after + terrestrial_ET + terminal_outflow
```

The balance includes coarse-owned L0 stores plus refined-owned L1 stores, never both independent copies of the same parent water.

## Persistence

`World::save()` continues to store world configuration and persistent L2 patches using the existing v1/v2 format.

Dynamic water remains an explicit simulation state. `save_multiresolution_water_state()` / `load_multiresolution_water_state()` use a separate versioned file containing the exact global day, coarse water state and sparse refined ownership. Derived hydrology topology is reconstructed from the world and authoritative continental topology on load.

Corrupt/truncated files, wrong-world identity, duplicate refined parents, clock mismatches, topology mismatches and trailing data are rejected.

## Scientific/model limitations

v0.6 establishes state ownership and conservative transfer, not a complete physical catchment model.

- Terrain/climate are synthetic scaffolding, not reconstructed Europe.
- Smooth forcing is **not weather**; a later WeatherSystem can replace it at the forcing boundary.
- Soil properties are still generic global bucket parameters, not spatial soil types. Uniform child depth during refinement is therefore deliberate; it does not claim spatial soil realism.
- L0 routing moves daily quickflow/baseflow through the DAG within one daily step; channel travel time and flood-wave hydraulics are not modeled.
- No lateral groundwater aquifers, wetlands, floodplains, channel geometry, erosion, sediment or vegetation feedback yet.

See `docs/MULTIRESOLUTION_WATER.md`, `docs/CONTINENTAL_WATER.md`, `docs/AUDIT_v0.5.md`, `docs/AUDIT_v0.4.md`, `docs/DYNAMIC_HYDROLOGY.md` and `docs/CONTINENTAL_HYDROLOGY.md`.

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

Europe-scale multiresolution benchmark:

```bash
./build/worldsim_multiresolution_water_benchmark
```

The benchmark has no pass/fail timing threshold; reported timings and RSS are environment-specific observations.

## CLI

Create a demo save:

```bash
./build/worldsim_cli demo demo.ws
```

Authoritative whole-world drainage:

```bash
./build/worldsim_cli continent demo.ws 25
```

Run the coarse world-scale water history:

```bash
./build/worldsim_cli continental-water demo.ws 365
```

Run the older standalone detailed L1 solver for one authoritative tile:

```bash
./build/worldsim_cli watercycle demo.ws 3 4 365
```

The v0.6 coupled ownership layer is currently exposed through the C++ API and its dedicated C ABI rather than a new CLI command.

## Audits

- `docs/AUDIT_v0.1.md` — spatial/persistence foundation before v0.2.
- `docs/AUDIT_v0.2.md` — regional hydrology before v0.3.
- `docs/AUDIT_v0.3.md` — authoritative drainage boundary before v0.4.
- `docs/AUDIT_v0.4.md` — rejects an L1-only scheduler and establishes the v0.5 L0 state boundary.
- `docs/AUDIT_v0.5.md` — validates the coarse boundary, records numeric/index hardening, and selects the v0.6 ownership model.

## Next bounded milestone

Do not add vegetation or erosion merely because multiresolution ownership now exists. The next natural dependency is spatial environmental state that genuinely needs finer heterogeneity — especially soil properties/capacity — while preserving the same parent/child conservation contract.
