# WorldSim v0.8.0 — capacity-aware multiresolution water

Headless C++20 simulation core for a large persistent world. v0.8 connects the deterministic v0.7 soil-property field to the authoritative L0/L1 water buckets while preserving one conservative dynamic-water truth across resolution changes.

## Implemented

- Engine-independent C++20 core (`worldsim`).
- C ABI suitable for thin Unity/Godot/Unreal adapters.
- Spatial hierarchy:
  - L0 climate / continental drainage / coarse dynamic water / parent-equivalent soil: 8192 m;
  - L1 regional terrain / authoritative refined drainage / selective detailed dynamic water / spatial soil heterogeneity: 1024 m;
  - L2 local persistent history: 64 m, 16×16 per L1 cell;
  - future entities use continuous coordinates.
- Europe-scale world bounds without eager L1/L2 allocation.
- Deterministic procedural terrain, climate and soil scaffolding.
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
- Deterministic spatial soil properties:
  - parent-equivalent L0 storage/infiltration scale factors;
  - heterogeneous L1 scale factors;
  - actual-area normalization so the L1 weighted mean reproduces the L0 parent property, including partial boundary parents;
  - queries remain derived and do not materialize or persist L1/L2 state.
- Capacity-aware soil-water dynamics:
  - storage scale applies to soil/field/wilting capacity and initial soil water;
  - infiltration scale applies to infiltration capacity;
  - both standalone L1 and whole-world L0 buckets use the same scaling contract;
  - local-capacity state is validated before mutation.
- Saturation-preserving L0→L1 soil-water transfer under heterogeneous child capacities.
- Atomic rejected mixed-resolution water steps.
- Separate versioned persistence for dynamic multiresolution water ownership; v0.8 writes format v2 and rejects old uniform-capacity format v1.
- Existing `World::save()` remains v2 with v1 read compatibility.
- Additive C ABI surfaces for multiresolution water and soil sampling; existing water POD layouts are unchanged.
- Deterministic smooth forcing helpers until a weather system exists.
- Lazy persistent L2 materialization and `disturb_surface()`.
- PR CI: GCC/Clang warnings-as-errors plus ASan/UBSan.
- Europe-scale mixed-resolution benchmark executable.

## Soil capacity invariant

`SoilProperties` contains dimensionless storage-capacity and infiltration-capacity modifiers. For every climate parent, the directly sampled L0 storage scale is the area-weighted mean of its in-world 8×8 L1 child scales:

```text
parent_scale = Σ(child_scale × child_overlap_area)
               --------------------------------------
                    Σ(child_overlap_area)
```

This remains true for partial parents at configured world boundaries. Because soil capacity is the reference capacity multiplied by that scale, the same relation holds for capacity itself.

The values are reproducible from seed + coordinates and are not persisted. The current soil field is synthetic scaffolding, not measured soil classes or reconstructed pedology.

## Capacity-aware refinement

When a coarse parent becomes detailed, soil depth is no longer copied uniformly. v0.8 preserves saturation:

```text
parent_saturation = parent_soil_water / parent_soil_capacity
child_soil_water  = parent_saturation × child_soil_capacity
```

Under the parent/child capacity invariant this conserves total soil-water volume over actual world overlap area and keeps every valid child within its capacity. Heterogeneous child capacities therefore produce heterogeneous child soil-water depths without creating or destroying water.

Snow, surface water and groundwater still transfer by parent depth because no spatial capacity field applies to those stores yet.

## Water ownership invariant

The same water volume is never independently owned by both L0 and L1.

```text
unrefined parent
    L0 stores = authoritative
    L1 tile   = absent

refined parent
    L0 stores = zero
    L1 tile   = authoritative
```

This gives the world one dynamic-water state with variable resolution rather than separate coarse and detailed simulations that can drift.

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

For each global mixed-resolution day:

```text
storage_before + terrestrial_precipitation
≈ storage_after + terrestrial_ET + terminal_outflow
```

The balance includes coarse-owned L0 stores plus refined-owned L1 stores, never both independent copies of the same parent water.

L0↔L1 transfer uses actual in-world overlap areas. Soil water uses saturation-preserving heterogeneous capacity transfer; the other conserved stores retain their existing volume-conserving depth transfer.

## Persistence

`World::save()` continues to store world configuration and persistent L2 patches using the existing v1/v2 format. Soil properties are derived static world truth and therefore are not serialized.

Dynamic water remains an explicit simulation state. `save_multiresolution_water_state()` / `load_multiresolution_water_state()` persist the exact global day, coarse water state and sparse refined ownership.

v0.8 changes the dynamic-water validity model from uniform to spatial soil capacity. Multiresolution-water files therefore use format v2. Format v1 is explicitly rejected rather than silently reinterpreted under the new capacity rules. No automatic migration is provided in this milestone.

Corrupt/truncated files, wrong-world identity, duplicate refined parents, clock mismatches, topology mismatches, local over-capacity soil state and trailing data are rejected.

## Scientific/model limitations

v0.8 establishes spatial bucket capacity, not a complete physical catchment or soil model.

- Terrain/climate/soil fields are synthetic scaffolding, not reconstructed Europe.
- Smooth forcing is **not weather**; a later WeatherSystem can replace it at the forcing boundary.
- Soil remains one vertically aggregated bucket with dimensionless synthetic modifiers, not horizons or measured retention curves.
- Infiltration remains a bounded bucket flux rather than unsaturated-flow physics.
- L0 routing moves daily quickflow/baseflow through the DAG within one daily step; channel travel time and flood-wave hydraulics are not modeled.
- No lateral groundwater aquifers, wetlands, floodplains, channel geometry, erosion, sediment or vegetation feedback yet.

See `docs/SOIL.md`, `docs/MULTIRESOLUTION_WATER.md`, `docs/CONTINENTAL_WATER.md`, `docs/AUDIT_v0.7.md`, `docs/AUDIT_v0.6.md`, `docs/AUDIT_v0.5.md`, `docs/DYNAMIC_HYDROLOGY.md` and `docs/CONTINENTAL_HYDROLOGY.md`.

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

The multiresolution ownership and soil-capacity layers are exposed through C++ APIs and dedicated C ABI extensions rather than new CLI commands.

## Audits

- `docs/AUDIT_v0.1.md` — spatial/persistence foundation before v0.2.
- `docs/AUDIT_v0.2.md` — regional hydrology before v0.3.
- `docs/AUDIT_v0.3.md` — authoritative drainage boundary before v0.4.
- `docs/AUDIT_v0.4.md` — rejects an L1-only scheduler and establishes the v0.5 L0 state boundary.
- `docs/AUDIT_v0.5.md` — validates the coarse boundary, records numeric/index hardening, and selects the v0.6 ownership model.
- `docs/AUDIT_v0.6.md` — validates ownership and selects the v0.7 spatial-property contract.
- `docs/AUDIT_v0.7.md` — validates the soil property contract and selects saturation-preserving capacity-aware water integration.

## Next bounded milestone

Do not infer vegetation or erosion from the existence of capacity-aware soil water. Select the next subsystem only after auditing v0.8 behavior and identifying the strongest remaining dependency; the current smooth forcing/weather boundary and simplified channel timing remain explicit candidates rather than assumed scope.
