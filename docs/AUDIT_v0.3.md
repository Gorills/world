# Audit of v0.3.0 before dynamic hydrology

## Scope reviewed

- whole-world L0 drainage;
- authoritative 8×8 L1 refinement;
- basin/outlet identity;
- L0→L1 conservation;
- C ABI exposure;
- save/load compatibility;
- suitability of v0.3 topology as a base for time-dependent water state.

## Material issue found

### Authoritative L1 tiles had no embedded world identity

`ContinentalHydrologyResult` already carried the `WorldConfig` that generated it, and refinement rejected a continental result from another world.

However, the returned `AuthoritativeHydrologyTile` only contained its climate coordinate and hydrology raster. Once detached from the continental result, a C++ caller could pass that tile to a future dynamic solver together with a different `World` having different bounds/seed/sea level. The types could not detect this misuse.

For static topology this was mostly latent. For dynamic water accounting it becomes material because cell overlap area, climate forcing and world bounds are read from `World` during stepping.

### Fix in v0.4

- `AuthoritativeHydrologyTile` now carries its originating `WorldConfig`.
- `DynamicHydrologyTileState` carries the same identity.
- state creation, forcing generation and stepping reject mismatched worlds.
- a regression test verifies the rejection.

## Reviewed but not treated as v0.3 defects

The following were documented v0.3 model boundaries and remain explicit limitations rather than silent bugs:

- coarse L0 coastline;
- no dedicated endorheic-basin physical class;
- no hydraulic-head continuity across independently refined tiles;
- climatological rather than time-dependent discharge;
- derived hydrology not stored in world save files.

Dynamic hydrology in v0.4 consumes the stable topology without claiming those limitations are solved.

## Result

No additional v0.3 blocker was found that required changing continental drainage before introducing a time-dependent water-storage layer.
