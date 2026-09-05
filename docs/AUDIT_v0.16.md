# v0.16 verification: autonomous ecosystem

Local verification used GCC 13.3, C++20, Release with `-Werror`.

- 38/38 CTest tests passed (including new ecosystem and C ABI tests).
- 38/38 passed under AddressSanitizer + UndefinedBehaviorSanitizer.
- LeakSanitizer could not run under this environment's ptrace sandbox. The ASan/
  UBSan run therefore used `ASAN_OPTIONS=detect_leaks=0`; leak detection is not
  claimed. Existing CI still enables leak detection on its own runners.
- A productive isolated cell and three coupled worlds each completed 36,500 days.
  The coupled worlds used seeds 42, 14002 and 4015, negative origins and partial
  boundary cells. Daily C/N accounting and century nitrogen inventory passed;
  vegetation and both animal guilds remained present without settlements or L2
  materialization. Maximum daily relative water residual across those worlds
  was below `5e-8` on the observed run.
- Drought, severe frost, hunger, excessive initial consumer biomass, empty life
  pools, all-ocean worlds and invalid environmental inputs passed focused checks.
- Coarse and refined PET both obey canopy factors. Refinement does not duplicate
  biomass, and ecosystem moisture reads the active water authority.
- Checkpoints round-trip byte-for-byte; repeated future days match exactly.
  Compound v1/v2 migrations, payload corruption and malformed ecological nutrient
  values are covered. Disturbance conserves carbon/N and repeating the same
  disturbance has no additional ecological effect.

Europe-scale benchmark, observed locally alongside other checks:

| Metric | Observation |
|---|---:|
| L0 cells | 449,208 |
| Refined water parents | 64 |
| L2 vegetation patches / settlements | 64 / 64 |
| Construction | 1,607.788 ms |
| Five unified days | 3,524.607 ms |
| Checkpoint save / load | 406.491 / 1,809.820 ms |
| Checkpoint size | 47,251,988 bytes |
| Peak RSS | 438,640 KiB |
| Maximum relative water residual | 5.882e-9 |

Every ecosystem cell is compared after reload and after one deterministic future
step, in addition to the pre-existing water/channel/vegetation/settlement checks.
Timings are observations, not performance guarantees. The ecology loop uses the
batch weather forcing API, which validates weather once per day; invoking the
single-cell weather API for every L0 cell would perform quadratic validation work.

The process assumptions, units and scientific scope are documented in
[ECOSYSTEM.md](ECOSYSTEM.md). These regressions demonstrate numerical stability
and persistence for their scenarios, not empirical calibration or guaranteed
survival in permanently unsuitable habitats. Individual animal agents and
aquatic food webs are outside this version's model.
