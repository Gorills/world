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

For each terrestrial L0 cell, the release is a fixed linear-reservoir fraction:

```text
release = channel_storage_at_day_start × 0.6321205588285577
```

This is the one-day response of a reservoir with a one-day e-folding time. The remaining start-of-day volume stays in the local channel store.

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

Multiresolution-water persistence is format **v3**.

v3 retains the existing world identity, hydrology parameters, day, coarse cells and sparse refined ownership, and adds one `double` channel volume per L0 cell.

The loader validates channel count/shape, finiteness, non-negativity and the invariant that ocean L0 cells own zero channel water.

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
```

The C ABI adds query functions for the standalone multiresolution-water handle and the unified simulation handle. Existing C structs and existing function signatures are unchanged.

The channel state remains mutation-controlled by the daily solver; the public APIs do not provide arbitrary setters.

## Regression and scale gates

Permanent regression coverage includes:

- no same-day release of newly generated runoff;
- one-edge delayed L0 transport through a refined parent;
- channel storage unchanged by materialize/aggregate ownership changes;
- invalid-step atomicity including the channel array;
- exact v3 water persistence round-trip;
- explicit v2 → zero-channel migration;
- corruption/non-finite channel rejection;
- C ABI channel queries and exact standalone save/load preservation;
- compound simulation C ABI channel preservation and exact future equivalence;
- the existing 60-day weather-driven mixed-resolution conservation regression, which detected the development-time downstream-arrival overwrite bug;
- Europe-scale compound checkpoint equality for every L0 channel cell before and after reload and after one deterministic future day.

The permanent Europe fixture contains 449,208 L0 cells and 64 refined parents. One GCC Release CI observation on the v0.11 implementation produced `85,772,959,568.875 m³` of channel storage after five warmup days and a maximum relative water-balance residual of `5.886e-9`.

Those values are observations, not API or performance guarantees.

## Deliberate limitations

v0.11 is a conserved travel-time model, not open-channel hydraulics.

It deliberately does not add:

- spatially varying reach residence time;
- channel width/depth/capacity or velocity derived from discharge/geometry;
- sub-daily flood-wave propagation;
- backwater effects;
- floodplain/wetland exchange;
- lateral groundwater-channel exchange;
- sediment or erosion feedback.

The fixed one-day reservoir is intentionally simple. A later routing milestone can replace the fixed coefficient with derived per-reach transport parameters without changing the ownership, conservation or checkpoint boundaries established here.
