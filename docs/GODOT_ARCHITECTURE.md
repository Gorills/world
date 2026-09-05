# Decision: Godot is an observer of the C++ simulation

Status: accepted for the initial desktop observer.

## Context

WorldSim already has an engine-independent C++20 library, compatible C ABI,
autonomous whole-world scheduling and compound checkpoints. A visual observer
must show that state without creating a second authority or changing stored data.

## Decision

Keep the existing core paths, CMake targets and save formats. Add an optional
GDExtension under `adapters/godot/` and a Godot project under `godot/`. The core
build does not download or link engine dependencies. The adapter depends on the
core; the core never includes Godot headers.

The extension owns `SimulationState` and returns copied, bounded render/query
data. The Godot scene owns camera, rendering and UI. Commands use the unified
simulation owner. Inspection has no persistent side effects. No mutable handles
to weather, water, ecosystem or world history cross into GDScript.

All calls are serialized on the main thread for this implementation. Loading
stages a replacement owner, then swaps it in. The existing checkpoint publisher
and validator remain the persistence boundary. Error results are explicit;
an unavailable extension produces a setup message instead of synthetic fallback
simulation data.

## Alternatives

- Reimplementing the model in GDScript would create divergent simulation truth.
- CLI subprocess/file polling would add synchronization, process lifetime and
  partial snapshot problems to interactive inspection.
- A C# P/Invoke client would reuse the C ABI, but require the .NET edition and a
  second managed binding surface. GDExtension works with the standard editor.
- Moving the whole core into the Godot project would couple its build/import
  lifecycle to the engine without improving simulation ownership.

## Consequences and limits

Godot-native binaries are platform-specific and must be built before opening the
observer. Official godot-cpp sources are pinned by commit and archive checksum.
The entry boundary catches exceptions, caps terrain allocation and rejects
invalid query coordinates. Existing checkpoint validation remains responsible
for file integrity; no custom serialization or network listener is introduced.

Observer graphics are a representation of the current model's resolution. L0
biomass is not a population of individual visible entities. Large world stepping
can block a rendered frame; worker ownership and immutable snapshot handoff are
future work if measured interaction latency requires it.

Per-cell Godot dictionaries are substantially larger than the core's compact
arrays. This first client limits accepted worlds to 4096 L0 cells and rejects
larger loads before replacing the owner; bounded regional streaming is deferred.

Rollback consists of disabling `WORLDSIM_BUILD_GODOT`. The CLI, existing C/C++
consumers and checkpoints remain compatible. Verification spans the core suite,
real engine import/start, exact native fixture comparisons, query immutability,
and failed-load preservation of the current generation.
