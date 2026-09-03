# Unified simulation state and checkpoints — v0.10

v0.10 introduces `SimulationState` as the lifecycle boundary for a running WorldSim world.

The goal is not to hide the existing subsystems. It is to ensure that persistent world history, transient weather and conserved multiresolution water can no longer be accidentally treated as unrelated save generations by normal application code.

## Authoritative ownership

A `SimulationState` owns:

- one `World`;
- one `WeatherState`;
- one `MultiresolutionWaterState`;
- the continental drainage topology derived from that `World`.

Continental topology is intentionally **derived state**, not a fourth persistence authority. It is rebuilt from the loaded `World` when a compound checkpoint is restored.

Public component accessors are const views. Runtime mutation is routed through simulation commands:

- `advance_day()`;
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

Daily stepping uses the existing atomic weather + multiresolution-water helper. Weather's next state is prepared before the water step and committed only after water succeeds. A rejected water step therefore cannot leave the unified owner with split clocks.

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

`from_world()` preserves the exact existing `World` configuration and materialized L2 history. Weather and water begin at aligned day zero because those transient authorities were not part of the legacy World file.

## Compound checkpoint format

A v0.10 checkpoint contains one container header followed by exactly three authoritative sections in fixed order:

1. World;
2. Weather;
3. Multiresolution Water.

The header stores:

- checkpoint magic and format version;
- one global signed 64-bit simulation day;
- fixed section count;
- for each section: identifier, byte length and FNV-1a checksum.

The section payloads are the existing versioned component formats. This deliberately reuses their validation instead of introducing a second serializer for the same state.

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

Component loaders retain their own wrong-world, malformed-state, capacity, ownership and trailing-data checks.

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

This is a validated atomic replacement contract. v0.10 does **not** claim full power-loss durability of the POSIX directory entry because the parent directory is not explicitly fsynced after rename.

## C ABI

`simulation_c_api.h` exposes one opaque `ws_simulation_state` handle.

The handle owns the complete unified state. It does not hand out mutable component handles. The ABI provides:

- create/destroy;
- global-day/L0/refined/L2 counts;
- regional and weather sampling;
- coarse/refined water copies;
- one-day advance;
- refinement and aggregation commands;
- persistent surface disturbance;
- compound checkpoint save/load;
- a simulation-specific thread-local error string.

Existing standalone World/weather/water C APIs remain source-compatible.

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

The v0.10 tests cover:

- exact global weather/water clock alignment;
- no accidental L2 materialization from construction, sampling or daily stepping;
- refined-water ownership through the unified command boundary;
- persistent L2 disturbance ownership;
- exact weather/coarse/refined round-trip state;
- byte-for-byte canonical reserialization of identical state;
- exact deterministic next-day evolution after reload;
- replacement of an existing checkpoint;
- checksum corruption, truncation and global/component clock mismatch rejection;
- migration of pre-v0.10 World L2 history;
- the opaque C ABI including error behavior and future equivalence;
- CLI `demo → simulation-run → simulation-resume` wiring on every CTest platform.

CI runs GCC and Clang with warnings-as-errors, ASan/UBSan, and the complete shared-library consumer suite on MSVC/Windows.

## Europe-scale checkpoint observation

The GCC Release CI benchmark uses the same 449,208-L0-cell Europe-scale fixture as the existing water/weather benchmarks, with 64 refined water parents and persistent L2 history.

One v0.10 CI observation measured approximately:

- unified simulation construction: `818 ms`;
- materialize 64 refined parents: `13.8 ms`;
- five unified days: `821 ms`;
- compound checkpoint save: `163 ms`;
- compound checkpoint load including topology reconstruction: `919 ms`;
- checkpoint size: `18,175,376 bytes` (`~17.33 MiB`);
- peak RSS during the benchmark: `229,872 KiB`;
- maximum relative water-balance residual: `5.895e-9`.

The benchmark also requires exact next-day equivalence between the original and reloaded simulations. These values are environment-specific observations, not performance guarantees.

## Format portability

The compound container and its component sections currently encode scalar fields through the project's existing native-POD binary persistence. Cross-endian or arbitrary cross-ABI file portability is not a v0.10 guarantee.

Changing that contract should be a deliberate persistence-format migration rather than an incidental change to the unified owner.
