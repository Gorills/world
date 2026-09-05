# Autonomous terrestrial ecosystem (v0.16)

`SimulationState` now seeds and advances a living ecosystem across the entire L0
land grid, including places never opened by a player. L1 water refinement and L2
materialization neither create nor duplicate ecosystem biomass. No settlements
or individual entities are required. Animals are continuous population biomass
for two functional guilds, herbivores and carnivores; they are not individually
positioned agents or named species.

## State and ownership

One `EcosystemCell` per L0 cell persists seven double-precision densities:

| Pool | Unit |
|---|---|
| Grass, shrubs, trees | kg C/m² |
| Herbivores, carnivores | kg C/m² |
| Litter/dead organic matter | kg C/m² |
| Available mineral nitrogen | kg N/m² |

Ocean cells own exactly zero terrestrial pools. Land area is clipped against the
world bounds, including negative origins and partial boundary cells. Derived
habitat metadata contains climate-dependent carrying capacities and actual soil
capacity. Initial plant stocks, litter and animals are deterministic functions
of the terrain/climate; initialization also applies existing local disturbance.

All organic pools have a common C:N mass ratio of 30. This simplification makes
nutrient accounting explicit but is not species-specific stoichiometry. Weather
is an external driver; atmosphere is an open carbon source/sink. Nitrogen is
closed: no deposition, fixation, leaching or denitrification is represented.

L2 `vegetation_biomass` remains the existing normalized local cover/recovery
proxy and disturbance history. It is not an additional carbon stock and is not
summed into ecosystem budgets. Consumers needing current plant/animal biomass
must query `simulation.ecosystem()` or the new C ABI, rather than static
`forest_potential` or the legacy L2 cover proxy.

## Daily processes

The following are numerical model assumptions, not empirically fitted rates.

1. Current weather supplies temperature and snow; authoritative coarse soil water
   or area-weighted active refined children supply saturation. Capacities include
   the existing spatial soil modifiers. Coarse stores of refined parents are
   never used as soil availability.
2. Litter decomposition increases with warmth and moisture. Carbon leaves as
   respiration, while its nitrogen returns to the mineral pool.
3. Plants lose biomass to litter through turnover, drought, severe frost and heat.
   Grass and shrubs are shaded by woody vegetation. Each group grows into its
   available carrying capacity; all groups share mineral N proportionally to
   demand so loop order cannot let the first plant group monopolize nutrients.
4. Herbivores eat grass and a fraction of shrub biomass. Snow hides ground forage,
   while woody browse remains accessible. Ingestion saturates with food and is
   reduced by competition among herbivores. Thirty percent of eaten carbon
   becomes consumer growth (including aggregate reproduction); the rest is litter.
5. Carnivore ingestion uses a type-III response to prey abundance plus predator
   interference. Sparse prey have low encounter rates; crowding reduces per-capita
   hunting success. Assimilation is 60%, with the remainder becoming litter.
6. Animal maintenance respires reserves, returning N to soil; baseline mortality
   transfers biomass to litter. Without food populations decline. There is no
   minimum population clamp, spontaneous birth or automatic reseeding.
7. Vegetative propagules and animals disperse between cardinal neighboring land
   cells. Each edge transfers mass once from the same post-reaction snapshot.
   Oceans and world boundaries are impermeable. Animal movement is diffusion,
   not individual pathfinding or directional resource-seeking migration.

For plant group `j`, after mortality:

```
room[j]   = max(0, shaded_capacity[j] - biomass[j])
demand[j] = min(room[j], biomass[j] * expm1(rate[j] * suitability * room[j]/capacity[j]))
scale     = min(1, 30 * mineral_N / sum(demand))
gain[j]   = demand[j] * scale
```

Zero capacities and zero demand are handled explicitly. Daily growth rates are
0.055, 0.015 and 0.0025 for grass, shrubs and trees. Fractional loss is computed as
`pool * -expm1(-rate)` and ingestion cannot exceed available food. There is no
unbounded explicit Euler subtraction from any pool.

Dispersal along edge `(i,j)` uses:

```
flux_kg = (density[i] - density[j]) * min(area[i], area[j]) * rate
```

Rates per edge/day are 0.0002, 0.00005, 0.00001, 0.01 and 0.02 for the five live
pools. With at most four edges, outgoing transfers cannot overdraw even a very
small boundary cell. Transfers carry the same C:N ratio as the source pool.

## Water feedback and accounting

Start-of-day live vegetation sets canopy cover and an ET demand multiplier:

```
cover  = 1 - exp(-(3*grass + 1.5*shrubs + 0.5*trees))
factor = 0.35 + 0.65*cover
```

The multiplier acts once on PET for both coarse cells and derived L1 forcing.
The existing soil bucket still limits actual evaporation/transpiration by water
availability and retains snow, infiltration, groundwater and river routing.
Meteorological PET queries remain reference atmospheric demand; they do not
include canopy effects. Standalone weather/water calls retain their previous
behavior unless an explicit multiplier vector is supplied.

Each complete day reports:

```
C_before + photosynthesis - respiration = C_after
N_before                               = N_after
water_before + precipitation           = water_after + ET + terminal_outflow
```

C and N reports include area-weighted whole-world stocks and fluxes. Closed
nitrogen bounds the total organic carbon (`C <= 30*N_total`); stable numerical
accounting is therefore stronger than clipping overflowing populations.

All next ecology, L2 and settlement states are staged before the environment
commits. Only no-throw swaps follow a successful water/weather step. Invalid
forcing cannot partially advance the unified generation. Surface disturbance
likewise stages both authorities and transfers the affected plant fraction to
litter. Repeating the same disturbance does not apply ecological damage twice.

## Persistence and APIs

Compound checkpoint v3 has five checksummed sections: World, Weather, Water,
Settlements and Ecosystem. The ecology section records model version 1, world
identity, day and seven densities per L0 cell; habitat metadata is rebuilt.
Invalid lengths, non-finite/negative pools, terrestrial stock in ocean cells and
clock/identity mismatches are rejected. Same-version continuation is exact.

Legacy compound v1/v2 checkpoints seed the new ecosystem deterministically at
the stored global day. They do not reconstruct a historical ecosystem trajectory
that was never saved. Existing water, weather, settlements and L2 history are
preserved; future unified water evolution now includes canopy feedback.

C++: `ecosystem().cell(coord)`, `ecosystem().totals()`, and
`advance_day_full().ecosystem`. C: `ws_simulation_ecosystem_cell`,
`ws_simulation_ecosystem_totals`, `ws_simulation_advance_day_v4`.
Existing C report layouts/signatures are unchanged; old daily advance entry points
also advance ecology. The C++ full-day report has an appended field; C++ clients
must rebuild against the new headers.

Create a pristine 128×128 km world and run it for 100 years:

```sh
./build/worldsim_cli simulation-new pristine.wsc 42 36500
./build/worldsim_cli simulation-resume pristine.wsc 365
```

## Verification and scope

`worldsim_ecosystem_tests` covers a productive single cell for 100 years and
three coupled worlds (seeds 42, 14002, 4015) for 36,500 days each. All five live
pools persist in the productive single-cell test, and both animal guilds and
plants persist in all three coupled worlds. Tests check daily C/N budgets,
century N inventory, water balance, drought, frost, hunger, extreme starting
animal densities, absence of spontaneous life, ocean exclusion, clipped areas,
refinement transitions, disturbance idempotence, old-save migration, corruption,
exact save/load continuation and invalid-forcing atomicity. The Europe benchmark
also compares every ecosystem cell after load and after the next day.

These are numerical regression guarantees for the tested scenarios, not proof
that every possible climate preserves every trophic level. Habitat loss and
permanent lack of food can correctly cause extinction. This is an aggregated
terrestrial model: no aquatic ecology, species/ages/genetics, explicit microbial
population, individual locomotion, fire spread, radiation budget or calibrated
animal counts. Further mechanisms need their own stock/flux accounting and tests.

Modeling references (motivation for the process structure, not calibration of
these constants): [QUINCY carbon/nutrient/water model](https://gmd.copernicus.org/articles/12/4781/2019/index.html),
[carbon and nitrogen conservation in CNit](https://gmd.copernicus.org/articles/18/2193/2025/gmd-18-2193-2025.html),
and [predator interference and food-web stability](https://pmc.ncbi.nlm.nih.gov/articles/PMC8844033/).
