# Persistent channel transport — v0.11

v0.11 adds conserved in-channel water to the authoritative mixed-resolution water state. The goal is narrow: remove same-day whole-DAG transport without introducing hydraulic geometry or a second water authority.

## Ownership

`MultiresolutionWaterState` remains the single dynamic-water authority. In addition to terrestrial snow, surface, soil and groundwater stores, it owns one non-negative `double` channel volume for every terrestrial L0 cell.

Channel storage is L0 state even when the parent terrestrial bucket is refined to L1. Materializing or aggregating a parent transfers only terrestrial bucket stores; it does not move, duplicate or reset the parent L0 channel volume.

Ocean L0 cells cannot own channel storage.

## Daily transport contract

A daily mixed-resolution step has two distinct channel generations:

1. channel storage present at the beginning of the day;
2. channel water arriving or generated during the current day.

Only generation 1 may release during that day.

For each terrestrial L0 cell, release remains a linear-reservoir response, but the residence time is now a bounded per-reach simulation heuristic:

```text
length_cells = 1 for cardinal D8, sqrt(2) for diagonal D8
slope        = max(downhill_gradient, 1e-5)
discharge    = max(accumulated_discharge_m3_s, 1)

residence_days = clamp(
    length_cells
    × (slope / 1e-5)^-0.08
    × (discharge / 100)^-0.06,
    0.75,
    3.0)

release_fraction = 1 - exp(-1 / residence_days)
release = channel_storage_at_day_start × release_fraction
```

D8 length is intentionally dominant. Filled-elevation slope and accumulated discharge only weakly alter residence, and the 0.75–3 day clamp prevents a daily global model from implying unsupported hydraulic precision. A flat cardinal reach at the 100 m³/s reference discharge retains the original one-day residence. The remaining start-of-day volume stays in the local channel store.

Current-day quick runoff/baseflow is added to the source L0 channel store only after release has been determined. It cannot be re-released until a later day.

Therefore one released parcel crosses at most one L0 edge per global day.

## L0 → L0 routing

For an unrefined downstream parent, an upstream release is added to the downstream next-day channel storage.

The downstream cell may independently release a fraction of the channel storage that it already owned at the start of the day. Its own release must not erase upstream volume arriving later in the same step.

This ordering is conservation-critical. During v0.11 development, the first implementation assigned the downstream residual after an upstream arrival and thereby overwrote that arrival. The existing 60-day weather/multiresolution conservation regression detected the resulting water loss. The final scheduler subtracts each cell's release from the scratch next-channel value, preserving arrivals that were already accumulated there.

## Refined-parent boundary

A released L0 volume entering a refined downstream parent is injected at the deterministic L1 ingress child selected by the existing authoritative tile connection rule.

Inside that parent, the volume traverses the existing L1 drainage graph together with that day's detailed runoff. The detailed solver's external outlet volume is then added to the **same parent's L0 channel store** for the next day.

It is not forwarded through the parent's L0 downstream relation during the same global step. Thus entering a refined parent does not bypass the one-L0-edge-per-day rule.

A refined parent's own current-day L1 runoff follows the same rule: its detailed external outlet becomes new parent L0 channel storage and waits for a later daily release.

## Terminal flow

If a released L0 channel volume has no terrestrial downstream cell, it contributes to `terminal_outflow_m3` and leaves authoritative world storage.

Current-day runoff at a terminal cell is first retained as channel storage and can only become terminal outflow on a later day.

## Conservation

Whole-world mixed-resolution storage is now:

```text
terrestrial coarse-owned bucket storage
+ terrestrial refined-owned bucket storage
+ all L0 channel storage
```

The daily balance remains:

```text
storage_before
+ terrestrial_precipitation
- terrestrial_evapotranspiration
- terminal_outflow
- storage_after
= water_balance_error
```

Channel transfers between L0 cells or through an L1 refined parent are internal transfers and therefore do not appear as external balance terms.

## Atomicity

The scheduler validates forcing, terrestrial state, channel shape, channel finiteness/non-negativity, clocks and numerical bounds before publishing the next generation.

Coarse bucket state, refined child state and channel volumes are accumulated in scratch storage. The authoritative state is committed only after the complete daily calculation succeeds. Rejected input therefore leaves terrestrial stores, channel stores, refined ownership and the global day unchanged.

## Persistence

Multiresolution-water persistence is format **v5**.

The authoritative storage layout remains the v3 layout: world identity, hydrology parameters, day, coarse cells, one `double` channel volume per L0 cell and sparse refined ownership. Derived transport metadata is not serialized.

The loader validates channel count/shape, finiteness, non-negativity and the invariant that ocean L0 cells own zero channel water. v3 fixed-reservoir and v4 length/slope files preserve all persisted water while deriving current v5 length/slope/discharge transport from the supplied authoritative topology.

Format v2 is migrated explicitly: v2 had no persistent in-channel state, so loading a valid v2 file creates the same terrestrial/refined state with every channel volume initialized to zero.

Format v1 remains rejected because it predates the spatial soil-capacity semantics introduced in v0.8.

Because the channel array belongs to the Multiresolution Water component, `SimulationState` compound checkpoints keep the same three-section authority model:

```text
World
Weather
Multiresolution Water (including channel storage)
```

There is no fourth channel section or independently coordinated save generation.

## C++ and C ABI observability

C++ exposes read-only channel queries:

```cpp
water.channel_storage_m3(climate_coord)
water.total_channel_storage_m3()
water.channel_transport(climate_coord)
```

`channel_transport()` reports reach length, downhill gradient, bounded residence days and daily release fraction. The C ABI exposes equivalent transport queries for the standalone multiresolution-water handle and the unified simulation handle.

The channel state remains mutation-controlled by the daily solver; the public APIs do not provide arbitrary setters.

## Regression and scale gates

Permanent regression coverage includes:

- no same-day release of newly generated runoff;
- one-edge delayed L0 transport through a refined parent;
- channel storage unchanged by materialize/aggregate ownership changes;
- invalid-step atomicity including the channel array;
- exact v5 water persistence round-trip;
- explicit v3/v4 → current-transport migration and v2 → zero-channel migration;
- corruption/non-finite channel rejection;
- C ABI channel queries and exact standalone save/load preservation;
- compound simulation C ABI channel preservation and exact future equivalence;
- the existing 60-day weather-driven mixed-resolution conservation regression, which detected the development-time downstream-arrival overwrite bug;
- Europe-scale compound checkpoint equality for every L0 channel cell before and after reload and after one deterministic future day.

The permanent Europe fixture contains 449,208 L0 cells and 64 refined parents. One GCC Release CI observation with the bounded residence heuristic produced `85,711,133,025.076 m³` of channel storage after five warmup days and a maximum relative water-balance residual of `5.886e-9`.

Those values are observations, not API or performance guarantees.

## Deliberate limitations

This remains a conserved travel-time model, not open-channel hydraulics.

The current per-reach residence formula is an intentionally weak simulation-scale heuristic. It is not calibrated against gauge-to-gauge travel times and does not claim that `accumulated_discharge_m3_s` plus filled-elevation slope reconstruct physical river velocity.

It deliberately does not add:

- sub-daily/multi-L0-edge flood-wave propagation;
- calibrated channel celerity;
- independent hydrograph lag and attenuation parameters;
- channel width/depth/capacity or Manning roughness;
- backwater effects;
- floodplain/wetland exchange;
- lateral groundwater-channel exchange;
- sediment or erosion feedback.

Further hydraulic detail should wait for a requirement that exceeds the daily one-edge model; otherwise additional calibration would imply precision the scheduler cannot express.
