# Architecture audit — v0.10

This audit reviews the accumulated WorldSim architecture after the v0.10 unified simulation/checkpoint work and before adding another conserved dynamic subsystem.

## 1. Scope reviewed

The audit covers the current ownership and integration chain:

- persistent `World` configuration and materialized L2 history;
- authoritative whole-world L0 drainage topology;
- deterministic L0/L1 soil properties and capacity-aware water buckets;
- authoritative coarse + sparse refined `MultiresolutionWaterState`;
- authoritative L0 `WeatherState`;
- the new `SimulationState` lifecycle boundary;
- component and compound persistence;
- C ABI ownership;
- CLI run/resume paths;
- Linux GCC/Clang, ASan/UBSan and Windows shared-library CI;
- Europe-scale benchmark behavior.

The v0.10 implementation changes lifecycle/persistence ownership. It does not change terrain, soil, weather or hydrology equations.

## 2. Unified runtime ownership

Before v0.10, `World`, `WeatherState` and `MultiresolutionWaterState` were valid explicit authorities but application code had to coordinate their identity, clocks and save generations.

v0.10 introduces `SimulationState` as the owner of one runnable simulation generation:

```text
SimulationState
    World
        persistent L2 history
        static/derived world truth
    derived ContinentalHydrologyResult
    WeatherState
    MultiresolutionWaterState
```

Continental topology is intentionally derived rather than serialized as a fourth authority. It is reconstructed from the loaded `World` before dynamic water is loaded.

External C++ callers receive const views of the owned components. Mutating operations that affect authoritative runtime state are routed through `SimulationState`:

- daily weather + water advance;
- refined-water materialization;
- refined-water aggregation;
- persistent surface disturbance;
- compound checkpoint save/load.

This removes the previous normal application path where weather and authoritative multiresolution water could be advanced independently by accident.

## 3. Global time invariant

The simulation-level invariant is:

```text
SimulationState.day
== WeatherState.day
== MultiresolutionWaterState.day
== every refined water tile day
```

`SimulationState::advance_day()` delegates to the existing atomic weather/multiresolution-water step and validates ownership before and after the command.

Compound load rejects a checkpoint whose container-level day differs from the loaded component clocks before exposing a `SimulationState`.

The model still uses exact signed 64-bit integer days; existing overflow rejection remains in the component schedulers.

## 4. Compound checkpoint format

The v0.10 simulation checkpoint contains exactly three authoritative sections:

1. `World` persistence bytes;
2. weather persistence bytes;
3. multiresolution-water persistence bytes.

The container adds:

- fixed magic/version;
- one global day;
- fixed section IDs/order;
- explicit section byte lengths;
- per-section streaming FNV-1a checksums.

The existing component formats remain responsible for their detailed semantic validation. This avoids duplicating world/weather/water serialization logic inside the container and preserves the independently tested loaders.

The checksum is an accidental-corruption detector, not an authentication or adversarial-integrity primitive.

## 5. Publication and failure behavior

Checkpoint save performs component serialization and compound assembly away from the target path. The assembled temporary checkpoint is fully re-read and checksum/length validated before publication.

The target checkpoint is therefore not truncated before a new complete generation exists.

Publication uses a same-directory temporary file so the final replacement stays on one filesystem:

- POSIX: rename replacement;
- Windows: `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`.

The completed temporary file is flushed before publication (`fsync` on POSIX, `FlushFileBuffers` on Windows).

During development, the Windows gate caught an invalid `FlushFileBuffers` use on a read-only handle. The final implementation opens the completed file with write access and the complete MSVC shared-library test suite passes.

Important limitation: on POSIX, v0.10 does not fsync the parent directory after rename. It therefore promises atomic replacement with a completed validated file under ordinary filesystem semantics, but does not claim full power-loss durability of the directory entry.

## 6. Load isolation and validation

Compound load does not expose partially reconstructed state.

It first validates container metadata, extracts/checksums the three sections, then:

1. loads `World`;
2. reconstructs continental topology from that world;
3. loads weather against the same world identity;
4. loads multiresolution water against that world/topology;
5. verifies global/container/component clock equality;
6. constructs `SimulationState`, which re-validates component/world identity.

Regression coverage rejects:

- invalid container magic/version/order;
- impossible section lengths;
- checksum corruption;
- truncation/trailing data;
- container/component clock mismatch;
- component-level malformed or wrong-world state through the existing loaders.

## 7. Legacy-world migration

`SimulationState::from_world(World)` is the explicit migration boundary for existing world saves.

It preserves the supplied `World`, including already materialized persistent L2 patches, derives current topology, and initializes day-zero weather/water authorities using the requested/default process parameters.

This is not a migration of old independent weather/water files into an arbitrary compound generation. It is specifically a legacy `World` → new day-zero unified simulation conversion.

A regression verifies exact persisted L2 disturbance history survives:

```text
legacy World
→ SimulationState::from_world
→ compound checkpoint
→ load checkpoint
```

## 8. C ABI boundary

v0.10 adds an opaque `ws_simulation_state` handle.

It owns the complete C++ `SimulationState`; it does not expose mutable component handles derived from that owner.

The additive API provides:

- create/destroy;
- global day and ownership counts;
- const-copy region/weather/coarse-water/refined-water queries;
- daily advance;
- refine/aggregate;
- persistent surface disturbance;
- compound save/load;
- independent thread-local error text.

The C regression verifies exact checkpoint round-trip and exact next-day evolution between original/reloaded handles.

Existing C ABI structs/signatures are not modified by v0.10.

## 9. CLI lifecycle

The authoritative executable path is now:

```text
legacy .ws
    simulation-run
        ↓
compound .wsc
    simulation-resume
        ↓
replace same .wsc with next generation
```

`simulation-run` uses `SimulationState::from_world`, so existing L2 world history is retained. A zero-day run can therefore be used as an explicit conversion to the compound format without advancing simulation time.

`simulation-resume` loads one compound generation, advances the requested number of days and atomically republishes the checkpoint.

CI contains an executable CTest chain for demo → simulation-run → simulation-resume rather than relying only on compilation of the CLI branch.

## 10. Determinism and conservation

v0.10 does not alter the weather or water equations.

The compound regression checks byte-for-byte canonical reserialization of an unchanged simulation on the same build/platform and exact future evolution after reload.

This remains a same-platform/file-format contract. WorldSim still does not promise bit-identical floating-point evolution or native-POD persistence compatibility across every architecture/compiler/endianness combination.

The existing mixed-resolution water ownership and conservation invariant is unchanged:

```text
coarse-owned parent: L0 stores authoritative, L1 absent
refined-owned parent: L0 stores zero, L1 stores authoritative
```

The Europe-scale v0.10 benchmark continues to enforce a finite daily balance and a maximum relative residual below `1e-6`.

## 11. Scaling observations

The permanent GCC CI benchmark uses:

- 449,208 L0 cells;
- 64 simultaneously refined water parents;
- persistent L2 history present in the checkpoint;
- five warm-up coupled days;
- compound save/load;
- exact sampled weather/refined state after reload;
- exact next-day future equivalence.

One audited CI observation on the final functional implementation measured approximately:

- simulation creation: 818 ms;
- materialize 64 refined parents: 13.8 ms;
- five coupled days: 821 ms;
- checkpoint save: 163 ms;
- checkpoint load including topology reconstruction: 919 ms;
- checkpoint size: 18,175,376 bytes (~17.33 MiB);
- peak RSS while holding original + loaded generations: 229,872 KiB;
- maximum relative water-balance residual: `5.895e-9`.

These are environment-specific observations, not performance guarantees.

The configured hard L0 limit remains 1,000,000 cells. The exact maximum-size simulation/checkpoint case is still not benchmarked and should not be described as verified.

## 12. Portability audit

Permanent CI now covers:

- GCC Release with warnings-as-errors;
- Clang Release with warnings-as-errors;
- ASan/UBSan;
- Windows MSVC shared-library build and all C/C++ consumers.

v0.9's Windows audit found test-harness file-lifetime/path issues and established the shared gate. v0.10 additionally exercised the new checkpoint flush/replacement path on Windows.

Persistence still writes native POD fields. Cross-endian and fully architecture-neutral save compatibility remain explicitly out of scope.

## 13. Remaining architectural debt

Ordered by ability to invalidate the next subsystem design:

1. **Same-day channel routing has no persistent in-channel storage/travel time.** Quickflow/baseflow can traverse the continental DAG within one daily step. This is now the strongest conserved-state omission.
2. **POSIX checkpoint publication lacks parent-directory fsync.** Atomic replacement is present, but strongest power-loss durability is not promised.
3. **Persistence encoding is native POD.** Portable endian/ABI-neutral files are not yet guaranteed.
4. **Weather remains L0-only.** Refined water uses parent L0 atmosphere; no elevation/orographic sub-grid weather exists.
5. **Groundwater has no lateral aquifer state and soil remains one vertical bucket.**
6. **C ABI implementation repeats some private handle/conversion helpers across translation units.** This is maintenance debt, not a public ABI defect.
7. **FNV section checksums are non-cryptographic.** Checkpoints are trusted local simulation files, not authenticated hostile input packages.

## 14. Selected next major slice: persistent channel transport

With v0.10, adding another dynamic authority no longer requires inventing another independently coordinated save lifecycle. The next major hydrologic dependency can therefore be the limitation deferred since v0.8/v0.9: persistent channel storage/travel time.

The next slice should be substantial and conservation-first, not a cosmetic discharge delay:

- introduce explicit L0 channel storage as conserved water;
- route only a bounded fraction of channel storage downstream per day, giving finite travel time rather than same-day whole-DAG traversal;
- define how refined L1 tile outlet water enters the parent/downstream L0 channel store without duplicate ownership;
- include channel storage in daily whole-world conservation accounting;
- make channel state part of `MultiresolutionWaterState` so it automatically lives inside `SimulationState` and compound checkpoints;
- advance multiresolution-water persistence format because conserved state changes;
- update C ABI state copies/reports only where a public observable is required;
- add long-river impulse/travel-time, steady-flow, refinement-boundary, checkpoint future-equivalence and Europe-scale regressions.

Do not add flood-wave hydraulics, variable channel geometry or sub-daily routing in the same slice unless the minimal conserved-storage model proves insufficient. The first target is to eliminate physically instantaneous daily routing while preserving deterministic ownership and water balance.

## 15. Audit conclusion

v0.10 closes the lifecycle/checkpoint dependency identified by the v0.9 audit. `World`, weather and multiresolution water now have one normal runtime owner, one exact simulation clock and one validated compound checkpoint generation.

No release-blocking ownership, conservation, ABI or tested portability defect remains in the audited v0.10 scope. The strongest remaining architectural issue is persistent channel transport, which can now be implemented inside the unified lifecycle instead of becoming a fourth independent authority.
