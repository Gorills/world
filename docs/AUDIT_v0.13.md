# Audit — v0.13 derived L1 atmospheric forcing

## 1. Scope

v0.13 closes the strongest atmosphere/water resolution mismatch left after v0.12.

Authoritative transient weather remains one L0 `WeatherState` at 8192 m. Selectively refined 1024 m L1 terrestrial water receives stateless child forcing derived from the current parent L0 forcing plus already-authoritative L1 terrain.

No persistent L1 atmospheric state, clock or checkpoint section is added.

## 2. Ownership

The authority graph remains:

```text
SimulationState
├── World
├── WeatherState               authoritative transient L0 atmosphere
└── MultiresolutionWaterState  authoritative conserved water
    ├── coarse/refined terrestrial stores
    └── persistent L0 channel storage
```

L1 atmospheric forcing is a derived diagnostic. It is not stored in `WeatherState`, `World` or `MultiresolutionWaterState`.

## 3. Temperature and PET transform

For each active terrestrial L1 child:

```text
parent_effective_elevation = max(0, parent_overlap_center_elevation)
child_effective_elevation  = max(0, child_terrain_elevation)

temperature_correction =
    clamp(-0.0065 × (child_effective_elevation - parent_effective_elevation),
          -8, +8)

child_temperature = parent_temperature + temperature_correction
child_PET         = max(0, 0.10 × (child_temperature + 5))
```

The 6.5 C/km lapse is bounded because this is a daily simulation-scale transform rather than an atmospheric column model.

The PET expression deliberately remains the existing temperature proxy; radiation, humidity and wind are still absent.

## 4. Precipitation redistribution

For active terrestrial children, the area-weighted mean child terrain elevation is computed over actual child/world overlap area.

Raw terrain weight:

```text
raw_weight =
    clamp(1 + 0.15 × (child_elevation - mean_child_elevation) / 1000,
          0.75,
          1.25)
```

Weights are normalized by actual child overlap area before multiplying the parent precipitation depth.

The public forcing representation uses `float`. After float conversion, the largest-overlap child is adjusted to the closest representable value minimizing aggregate parent-volume residual. Thus parent precipitation volume is conserved to the precision available in the public float forcing ABI, including partial world-boundary parents.

This is a weak deterministic terrain redistribution. It does not claim windward/leeward physics.

## 5. Coupling

`advance_multiresolution_water_day()` applies the same pure transform to any valid L0 forcing provider.

Therefore:

- authoritative `WeatherState` forcing receives the transform;
- controlled smooth/legacy forcing receives the same transform;
- coarse-owned L0 water is unchanged;
- refined water no longer receives 64 identical forcing records.

Only the forcing values change. Refined water ownership, routing, channel causality and exact integer clocks remain unchanged.

## 6. Public API and C ABI

C++ exposes:

```cpp
derive_refined_atmospheric_forcing(...)
SimulationState::refined_daily_forcing(...)
```

Both are read-only and require an already-refined parent.

The standalone multiresolution-water C ABI accepts an explicit parent L0 forcing record and copies the derived 64-slot L1 vector.

The unified simulation C ABI copies the current `WeatherState`-derived L1 forcing vector.

Queries do not advance clocks or materialize L2 persistent state.

## 7. Persistence migration

Multiresolution-water persistence advances to semantic format **v6** without changing the authoritative byte layout introduced by v3.

No L1 forcing values or coefficients are serialized.

Migration behavior:

- v2: terrestrial/refined water is preserved; channel storage initializes to zero because v2 had no channel authority;
- v3-v5: all persisted water/channel/refined ownership is preserved exactly;
- after load, future refined evolution uses current v6 stateless forcing semantics;
- v6: current semantics.

The format bump is semantic because a v5 checkpoint containing refined tiles will evolve differently under the new derived forcing even though its persisted authoritative state is unchanged.

## 8. Regression requirements

Permanent coverage includes:

- partial-boundary parent overlap;
- 64-slot coordinate alignment;
- inactive-child zero forcing;
- finite/non-negative hydrology-safe values;
- bounded elevation lapse and temperature-derived PET;
- non-uniform terrain precipitation;
- aggregate parent precipitation conservation to float-forcing precision;
- no L2 materialization from forcing queries;
- invalid parent forcing rejection without clock mutation;
- scheduler use of the same transform;
- v5 -> v6 semantic migration;
- exact deterministic future equivalence between current v6 and migrated-v5 state;
- standalone and unified C ABI capacity/error/query behavior;
- existing Europe-scale multiresolution/weather/checkpoint gates with 64 refined parents.

## 9. Deliberate limitations

v0.13 does not add:

- persistent L1/L2 atmosphere;
- pressure or wind;
- humidity/dew point;
- radiation or cloud physics;
- physical windward/leeward precipitation;
- numerical weather prediction;
- climate drift.

## 10. Next decision boundary

Atmosphere now matches selective hydrology refinement without duplicating state authority.

The strongest remaining land-water simplifications are lateral groundwater and the single vertically aggregated soil bucket. However, the environment stack is also mature enough to justify moving upward into vegetation/ecology/entity state.

The next milestone should be selected from an explicit consumer requirement rather than deepening weather or channel physics by default.

## 11. Audit conclusion

v0.13 is intentionally a forcing-resolution feature, not a new atmospheric model.

It uses existing terrain and L0 weather truth to make refined hydrology spatially meaningful while preserving the established sparse-state, conservation, persistence and unified-clock boundaries.
