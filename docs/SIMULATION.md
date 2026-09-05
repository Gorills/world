# Unified simulation state and checkpoints — v0.14 lifecycle

v0.10 introduced `SimulationState` as the lifecycle boundary for a running WorldSim world. v0.14 keeps the same three-component checkpoint authority while extending the World section with sparse persistent L2 vegetation. Channel and refined-forcing contracts remain documented separately in `docs/CHANNEL_TRANSPORT.md` and `docs/WEATHER.md`.

The goal is not to hide the existing subsystems. It is to ensure that persistent world history, transient weather and conserved multiresolution water can no longer be accidentally treated as unrelated save generations by normal application code.

## Authoritative ownership

A `SimulationState` owns:

- one `World`, including sparse L2 disturbance + vegetation history;
- one `WeatherState`;
- one `MultiresolutionWaterState`, including persistent L0 channel storage;
- the continental drainage topology derived from that `World`.

Continental topology is intentionally **derived state**, not a fourth persistence authority. Channel storage is likewise not a fourth component: it is conserved water owned by `MultiresolutionWaterState`.

Public component accessors are const views. Runtime mutation is routed through simulation commands:

- `advance_day()` / additive `advance_day_full()`;
- `materialize_refined_water_tile()`;
- `aggregate_refined_water_tile()`;
- `disturb_surface()`;
- `save_checkpoint()`.

This does not remove the lower-level APIs; they remain useful for focused solvers/tests and compatibility. New application-level evolving-world code should prefer `SimulationState` when weather and multiresolution water are both authoritative.

## One exact clock

The simulation-level invariant is:

```text
SimulationState::simulated_day()
== WeatherState::simulated_day()
== MultiresolutionWaterState::simulated_day()
== every refined water tile day
```

Daily stepping derives vegetation forcing from current weather/water and advances a staged copy of sparse World history first. The existing weather + multiresolution-water step then commits atomically. Only after that succeeds does a no-throw local-history swap publish the staged vegetation generation. A rejected environmental step therefore cannot leave vegetation ahead of weather/water.

The constructor and checkpoint loader reject inconsistent component identity or clock state before exposing a `SimulationState`.

## Starting a simulation

A new simulation can be created directly from `WorldConfig`:

```cpp
worldsim::SimulationState simulation(config);
```

For migration from pre-v0.10 `World::save()` files:

```cpp
auto world = worldsim::World::load("legacy.ws");
auto simulation = worldsim::SimulationState::from_world(std::move(world));
```

`from_world()` preserves the exact existing `World` configuration and materialized L2 history. World v1/v2 patches migrate deterministic vegetation biomass from their persisted forest potential/disturbance. Weather and water begin at aligned day zero because those transient authorities were not part of the legacy World file; channel storage likewise starts at zero for legacy World-only saves.

## Compound checkpoint format

A simulation checkpoint contains one container header followed by exactly three authoritative sections in fixed order:

1. World;
2. Weather;
3. Multiresolution Water.

The header stores:

- checkpoint magic and format version;
- one global signed 64-bit simulation day;
- fixed section count;
- for each section: identifier, byte length and FNV-1a checksum.

The section payloads are the existing versioned component formats. This deliberately reuses their validation instead of introducing a second serializer for the same state.

v0.14 still has exactly three sections. Persistent vegetation is serialized inside World format v3; channel storage remains inside Multiresolution Water. Neither feature creates another checkpoint authority.

The topology is not serialized. On load the sequence is:

```text
validate container header/lengths
        ↓
verify checksums while extracting sections
        ↓
load World
        ↓
rebuild continental topology from World
        ↓
load Weather against that World identity
        ↓
load Multiresolution Water against World + topology
        ↓
require component clocks == checkpoint global day
        ↓
expose SimulationState
```

Component loaders retain their own wrong-world, malformed-state, capacity, ownership and trailing-data checks. World v3 validates normalized vegetation state and migrates v1/v2 local history; multiresolution-water v6 retains its existing channel/forcing migrations.

FNV-1a is used for accidental-corruption detection only. It is not a cryptographic authenticity mechanism and checkpoints must not be treated as secure against a maliciously constructed file.

## Publication semantics

`save_checkpoint()` never truncates the current target first.

It:

1. validates the in-memory simulation invariants;
2. serializes the three component sections to private temporary files;
3. measures and checksums them;
4. assembles a complete checkpoint into a temporary file in the target directory;
5. re-reads and validates the assembled container;
6. flushes the completed temporary file (`fsync` on POSIX, `FlushFileBuffers` on Windows);
7. atomically replaces the target (`rename` on POSIX, `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows).

Failures before the final publication leave the previous target untouched.

This is a validated atomic replacement contract. WorldSim does **not** claim full power-loss durability of the POSIX directory entry because the parent directory is not explicitly fsynced after rename.

## C ABI

`simulation_c_api.h` exposes one opaque `ws_simulation_state` handle.

The handle owns the complete unified state. It does not hand out mutable component handles. The ABI provides:

- create/destroy;
- global-day/L0/refined/L2 counts;
- regional and weather sampling;
- coarse/refined water copies;
- read-only channel/transport and refined-forcing queries;
- read-only local vegetation copy for existing materialized patches;
- legacy one-day advance plus additive `ws_simulation_advance_day_v2` environment+vegetation report;
- refinement and aggregation commands;
- persistent surface disturbance;
- compound checkpoint save/load;
- a simulation-specific thread-local error string.

Existing standalone World/weather/water C APIs remain source-compatible. v0.14 adds separate vegetation PODs/functions and does not extend or reorder older C structs/signatures.

## CLI migration and resume

Create a compound checkpoint from an existing World save and advance it:

```bash
worldsim_cli simulation-run legacy.ws campaign.wsc 30
```

Resume the same authoritative generation and atomically replace the checkpoint after advancing:

```bash
worldsim_cli simulation-resume campaign.wsc 30
```

`simulation-run` preserves legacy L2 history through `SimulationState::from_world()`. `simulation-resume` loads the compound checkpoint rather than independently reconstructing weather/water files.

A zero-day run is accepted when an existing World save only needs to be migrated into a compound checkpoint.

## Regression coverage

The simulation/checkpoint tests cover:

- exact global weather/water clock alignment;
- no accidental L2 materialization from construction, sampling or daily stepping;
- refined-water ownership through the unified command boundary;
- persistent L2 disturbance + vegetation ownership;
- exact weather/coarse/refined round-trip state;
- exact channel state through standalone and compound persistence;
- World v1/v2 → v3 vegetation migration;
- byte-for-byte canonical reserialization of identical state on the same build/platform;
- exact deterministic next-day evolution after reload, including channel and vegetation state;
- replacement of an existing checkpoint;
- checksum corruption, truncation and global/component clock mismatch rejection;
- migration of pre-v0.10 World L2 history;
- the opaque C ABI including error behavior and channel/future equivalence;
- CLI `demo → simulation-run → simulation-resume` wiring on every CTest platform.

CI runs GCC and Clang with warnings-as-errors, ASan/UBSan, and the complete shared-library consumer suite on MSVC/Windows.

## Europe-scale checkpoint observation

The GCC Release CI benchmark uses the same 449,208-L0-cell Europe-scale fixture as the water/weather benchmarks, with 64 refined water parents and 64 materialized vegetation patches (16,384 L2 cells).

One v0.14 CI observation measured approximately:

- unified simulation construction: `671.042 ms`;
- materialize 64 refined parents: `10.747 ms`;
- five unified environment + vegetation days: `739.681 ms`;
- compound checkpoint save: `386.885 ms`;
- compound checkpoint load including topology reconstruction: `751.586 ms`;
- checkpoint size: `22,093,640 bytes` (`~21.07 MiB`);
- persistent channel storage after five warmup days: `85,711,133,025.076 m³`;
- peak RSS during the benchmark: `270,440 KiB`;
- maximum relative water-balance residual: `5.886e-9`.

The benchmark requires exact channel equality across every L0 cell plus exact equality of all 64 vegetation patches after reload and after one deterministic future day. These values are environment-specific observations, not performance guarantees.

## Format portability

The compound container and its component sections currently encode scalar fields through the project's existing native-POD binary persistence. Cross-endian or arbitrary cross-ABI file portability is not a guarantee.

Changing that contract should be a deliberate persistence-format migration rather than an incidental change to the unified owner.


## Settlement integration (v0.15)

`SimulationState::found_settlement()` creates a sparse persistent entity with deterministic monotonic identity and the current global day as `founded_day`. One regional cell may own at most one settlement.

`SimulationState::settlement_suitability()` is a current-day derived diagnostic. It combines bounded simulation-scale heuristic factors for terrain, soil-water availability, vegetation biomass, temperature and disturbance. These factors are not empirical demographic calibration and are never serialized.

Daily population change is deliberately weak and bounded: population moves slowly toward the derived environmental capacity, with independent growth/decline clamps and a hard non-negative/finite invariant. There is no migration, trade, roads, agriculture, demographics or settlement-to-settlement interaction in v0.15.

Compound checkpoints use container format v2 with a fourth settlement section. Format-v1 checkpoints are accepted as an explicit migration path and construct an empty settlement authority.
