# v1.0.4 validation and device benchmark protocol

These measurements exercise algorithms on this execution machine, not GD.
They do not establish the supplied plan's mobile runtime targets.

| Synthetic fixture | Old probes | New probes | Reduction | Exact windows | Lower bounds |
|---|---:|---:|---:|---:|---:|
| 20 events | 180 | 95 | 47.22% | 15 | 5 |
| 154 events | 1374 | 728 | 47.02% | 116 | 38 |

Fixture windows cycle through widths 1, 3, 5 and an open wide window. The new
search reports the wide window as 8+ rather than continuing to radius 12.
The lower-bound output deliberately contains less information. Narrow widths
match the exhaustive connected window; exhaustive tests include real holes.

Release build on the execution machine:

- 1000-click final Base solve: approximately 0.19 ms, under the plan's 50 ms target.
- All 1000 cumulative prefixes / eight models: approximately 122 ms with verified warm starts, separate
  from the final-only solve and therefore run outside the game thread.
- LUT maximum absolute error against std::erfc: 4.36e-10.
- All eight weights are checked against an independent long-double std::erf
  oracle with bisection, in addition to the one-click closed-form reference.
- Reproduce with tests/benchmark.cpp (-O2) and tests/precision.cpp. CI prints its
  own times; runtime is reported instead of a flaky hardware-dependent assertion.
- Android CI configures the mod with CMAKE_BUILD_TYPE=Release (confirmed in run
  43018346081), independently of the standalone benchmark's explicit -O2 flag.

## Required on-device checks (not executed here)

There is no running Geometry Dash/Geode process or level geometry in this
workspace. The attached plan alone contains no scan-report.txt measurements.

The two earlier user CFR examples (1539950219185091717 and
1962885543127956795) were read without modification with tools/precision_compare.cpp.
Both contain zero measured windows. The tool deliberately refuses to invent L*
from click spacing. A real per-run numerical comparison needs measured windows;
the in-game precision.csv export produces it after Analyze.

Build the read-only comparison tool with:

```sh
g++ -O2 -std=c++20 tools/precision_compare.cpp -o precision_compare
./precision_compare run.cfr
```

1. Use the identical saved capture and input mode for a short and a long level.
   Start fresh analysis after setting the policy, and preserve scan-report.txt,
   scan-events.tsv and scan-phases.csv for each run.
2. Check tests/event, candidate callbacks/attempt, restore ratio, reset time,
   audit calls, local exits and fallback causes. Reference/probe/confirmation/
   recovery work is separated so baseline overhead does not masquerade as
   candidate cost. A batch fallback must be diagnosed before raising budgets.
3. On three different levels, compare Local scope with full-route scope using
   cap 0, the same radius and the same capture. Require no narrow-window
   discrepancies; sampled audit mismatches must invalidate local results.
4. Separately compare cap 8 and cap 0: widths below the cap must agree; 8+
   means at least eight valid ticks and must never be treated as exact eight.
5. Repeat with native cache and batching off to isolate restored-state and
   cadence effects. Reproduce any crash with its stack and fallback report.
6. In Play, compare prefix precision.csv values with the source mathematical
   model at identical TPS/parameters. Interval ms, window ms and sigma ms answer
   different questions and should not be numerically equated. Edited output
   must remain labeled and cannot overwrite measured precision.csv.
7. Pause/cancel/resume, change levels or settings during calculation, edit/save,
   and finish the showcase through native completion. Old worker results must
   never attach to the new level or reappear on the completion screen.

Do not enable direct loadCheckpoint, cross-offset state reuse or aggressive
failure heuristics based on these synthetic results. Those require engine data.
