# Click Frames 1.0.1 — experimental

The v1.0.0 comparator included sprite rotation/scale and inactive P2. These are
not reliable gameplay state. v1.0.1 compares vehicleSize/playerSpeed, skips
inactive P2 and visual rotations, and uses small absolute rounding tolerances.
Every failed comparison writes the first differing field with 17-digit values.
The scheduler now records the first run's dt sequence and repeats that grouping
at both normal and batched cadence. Native states carry the scheduler cursor,
shared fast_srand state, copied GJGameState, copied collision dictionaries and
the causal cursor. Copies remain per-session and are never raw memory dumps.

Causal instrumentation covers ringJump invocations, touched triggers, pads,
teleports, toggles, game events and pickups. These are observed calls, not a
complete graph of all trigger overrides. Baselines/restores require exact event
tick and order; candidates require matching order for local acceptance. Flight
recovery changes one neighboring input +/-1 and reports evidence separately.
It never widens the fixed-route window. Snapshot universality, complete causal
coverage, analytical wave geometry and fractional CBF remain unimplemented.

The scan scheduler runs the ordinary scheduler chain in bounded 8 ms batches,
at most 256 fixed 1/240 s slices. The first baseline uses ordinary cadence;
two repeats compare pre-command observations. A mismatch rebuilds without
batching; a second mismatch aborts without producing a window. No direct
GJBaseGameLayer update bypass or concurrent physics threads are used.
This advances all scheduled targets and cannot isolate private mod state.
Native snapshots retain native GD checkpoint coverage, not universal engine
coverage. Local rejoin compares observable player state for four grounded
ticks, not trigger equivalence. Local horizons remain explicitly scoped.
Wave corridor and flight recovery algorithms and fractional CBF are pending.

## Successful showcase completion

Playback now intercepts successful completion separately from the trial state
machine: verify all presses were dispatched, stop the mod session, clear its HUD,
and call the original PlayLayer::levelComplete once within the hook. It never
queues a reset on success. Practice mode is preserved and the native game selects
its completion UI. Pending failure still blocks success. Record/Analyze are
unchanged. resetLevel/resetTrial clear showcaseComplete for later attempts.

## Split bundle and scope migration

Bundle.hpp owns CFCAP 1 (capture.flr), CFPLAY 1 (playback.flp), bounded reads,
safe replacement and legacy migration. Capture stores original inputs and trace;
playback carries per-index tick overrides, windows, verified status, radius and
policy, bound to the capture hash. It may drop trailing releases but not presses.
Both parsers validate before mutating the destination. Commit playback first,
capture last; loader tries the matching playback.bak if a generation is interrupted.
Legacy sources are never removed. Capture serialization is skipped entirely when
captureDirty is false. Derived traces only regenerate on capture change/load;
predictions flush at stop/completion/exit. Reports survive a fresh session load.

Editor TSV is the new active visual sidecar, named by trace hash. JSON is a legacy
fallback and retained as an original. CFPACK 2 adds flat edits-<key>-<trace>.tsv
members alongside portable run-<key>.cfr. Import validates all runs and attached
edits before writing. Disk failures during a multi-run import may leave a partial
import with backups and the .cfpack in place for retry; there is no multi-bundle
transaction. Capture transport uses CFR 5 for policy 2 and CFR 4 otherwise.

Policy 2 measures a local segment, not the full run. Verify captures reference
physics points BEFORE input dispatch and first landings at physics boundaries.
An analyzed candidate can pass at the matching first landing, or before the next
counted same-player action if it matches the reference mode/gravity/position zone.
Missing reference/physics samples fall back to full-route validation. Policy 0/1
retains full-route requirements. Scope changes invalidate windows only on explicit
Analyze, preserving loaded progress and editor behavior.

Native cache pools up to eight checkpoint objects captured in the same Verify
pass. A chosen state is strictly before target-radius-2, validates its original
suffix once against post-update reference positions and completion tick, and can
serve many events. The first cached boundary failure per state is confirmed from
zero; a disagreement rejects it. Native state remains opt-in, not a universal
proof. No batched/headless engine update loop is introduced in this release.

## Visual editor

Editor.hpp owns pure trajectory interpolation, bounded marker JSON, arc-length
chains, counter conversion and 32-step undo/redo. CFEditor edits a draft while the
real level is paused; its canvas shows sampled geometry, never a seeked live
PlayLayer. Seek changes only the cursor; Move applies it to the selected marker.
P1/P2 and scalar time define position. Teleport-size segments use nearest endpoint
instead of interpolation, and chains skip those discontinuities.

Save writes showcase-edits.json through writeSafe and updates Session::visual only
after disk success. Fingerprint plus trace hash prevent applying edits to another
capture. capture.cfr stays authoritative for physics; edits never enter cf::read
or the analyzer. Visual playback has an independent sorted marker cursor, optional
count/sound and separate row generation. Measured trails are suppressed in edited
mode to avoid mixing simulated and illustrative outputs. Session Record clears
the active visual document; old files remain recoverable on disk.

Native state reuse additionally captures/restores GD input queues, RNG/replay RNG,
currentStep, extraDelta, queued-recorded count and resume timer. These are process-
local snapshots, still opt-in and checked against the baseline. This is not yet a
proof of universal GD-state equivalence. Fractional CBF replay and isolated live
prediction remain unimplemented and must not be advertised as completed.

## 0.8 analysis engine boundaries

ContinuousSearch traverses the connected success interval around zero. Tests
compare it exhaustively to measure() for every outcome mask up to radius 6.
Invalid candidates count as failures without simulation. Native states are one
retained CheckpointObject per target, captured strictly before its earliest test
input. Baseline capture and native restore are separate automated runs. Restore
must match the saved tick, sampled P1/P2 positions and original completion tick.
On mismatch the state is discarded. Cached failures are rerun from zero. This
does not constitute a universal full-state proof, hence opt-in only.

Landing references are generated anew by Verify, using the first airborne-to-ground
transition before the next press in cube/robot. Platform identity is a runtime
pointer and never serialized. No landing reference means full-route validation.
Candidates must match the platform and a 60x6 unit position tolerance, then finish
the level. This is a bounded landing corridor, not semantic obstacle recognition.

Trajectory previews are bounded measured candidate traces, not live isolated-world
simulation. Hash-keyed prediction-trails.txt is derivative and optional. Native
checkpoint pointers never go to disk. The three-branch/32768-point budget bounds
memory; exact replays require an unchanged capture/profile. CFR 4 stores policy
and vanilla240/cos240 input profile; CBF substep replay is not implemented. An
external CBF provider needs a real substep replay API or an equivalent validated
integration, not a wall-clock timestamp bolted onto integer-tick injection.

## Showcase event policy and units

CFR 3 adds an integer mode after each input position. Readers accept CFR 2
with unknown mode (-1). All inputs remain in injection and timing-order checks;
counted() selects jump presses and releases in ship/wave/robot/swing for analysis
and presentation. Cube/ball/UFO/spider and unknown-mode releases are excluded.
Old measured releases remain on disk but no longer affect rows or readiness.
Newly relevant events must be measured before a capture is ready.

Markers use transparent fill and a width-only label. The + lower-bound signal
is retained. The ms HUD shows the tick interval between jump presses, updates
each press after the first, and pulses for 60 simulation ticks. Coordinates are
percentages of usable screen area, recomputed each playback service update.
Width includes zero: -5 through 0 means six 240 Hz ticks, not six display frames.

## Per-level storage

Active captures use recordings/<slug-id>/<fingerprint>/capture.cfr. Legacy root
and levels/<slug-id>/run-<fingerprint>.cfr files are validated and copied through
a temporary file on level open; originals are retained. Imports write the new
layout while the CFPACK 1 wire format stays unchanged. Opening a capture creates
metadata.json, inputs.csv, trace.csv, windows.csv and scan-report.txt. These files
are derived views and are never loaded as gameplay state. An incomplete derived
write does not invalidate the authoritative capture. Unchanged derived content
is cached per session to avoid repeated disk writes. CSV generation still costs
CPU; snapshots and early-stop analysis are future work, not enabled here.

## 0.2.1 completion handoff

Record, Analyze and Playback no longer call `pauseGame` directly from a level
completion callback. They save their state, queue a clean restart without
checkpoints, then open pause on the following main-thread cycle. This prevents
the game from remaining at 100% with gameplay and pause controls unresponsive.

## Timing markers and dynamic counter

Recording format 2 stores the exact world position of every press and release.
Format 1 recordings are intentionally rejected because they cannot place markers
reliably; the popup asks for a new run. Finishing Record saves and pauses. Analysis
starts only from Frames > Analyze / Resume.

The analyzer measures the contiguous full-route success window around the original
tick. Showcase markers stay in the object layer until the run ends or restarts and
display both total width and `-early / +late`. Presses use a filled circle; releases
use an `R` and a nearly transparent circle. Both play their width sound and both
increment the counter. Counter rows are preselected from the completed analysis,
so empty categories—including 1F when there are no frame perfect events—take no
space and the layout does not jump during playback.

Physical and injected held-button states are separate. Record never synthesizes
button transitions. Ending or cancelling an automated run only releases buttons
actually injected by the mod. A manual restart cancels automation and then calls
the game's normal reset path instead of swallowing it.

## Record crash fix

The reported Android64 v0.1.0 instruction at offset 0x2fb4c dereferenced the
session owner plus 0x310e (platformer flag). Its owner was null. Session binding
now happens after PlayLayer initialization, and onExit only saves: a scene exit
does not prove destruction. The popup reacquires the active PlayLayer when opened
and before any session action; absent or stale owners cannot reach field reads.
The Frames button uses its own bottom-center menu at y=35.

Device regression checks: open pause and Record on a newly opened level; repeat
after leaving/re-entering a level and after opening/closing pause several times;
repeat with other UI mods enabled. A missing active level must show a message
instead of crashing. Check bottom-center placement on both narrow and wide screens.

## Flow

Pause > Frames > Record starts a new practice run from zero. Checkpoints keep
the input prefix, trace prefix and held buttons; a retry discards the failed
suffix. Finishing saves the run and returns to pause. Analyze starts a baseline replay. Verification passes when the replay finishes the
level and has replayed every press; a trajectory drift over 0.5 units is only
logged (`[CF] first position drift`). A death, a timeout or an unsupported step ends
it. Before giving up, the whole run is retried shifted by +1, -1, +2, -2 ticks (the
recorded tick and the replay's injection point can differ by a step); the shift that
works is saved into the run. The reason for a failure is shown in an alert when pause
returns and in the Geode log with the `[CF]` prefix. Only a verified run can be analyzed.

Analyze / Resume verifies the baseline again and resumes at the first unmeasured
event. Each jump press and release is independently shifted from -radius through
+radius (zero uses the verified baseline). Trials run from the beginning and
must complete the level. Inputs on the same player/button cannot change order.
The displayed window is the contiguous passing interval containing zero; isolated
passing timings outside it do not count. A window touching a search boundary is
a lower bound, displayed with `+`. One valid tick is a frame perfect; zero is not
a successful window. Other inputs stay fixed, so results are conditional on this run.

Play / Replay requires all windows to be measured. Presses increment the five
counter rows, releases display `R` but do not increment them. Sounds, marker size,
counter size and radius are configurable. Stop / Save cancels immediately and
keeps completed event results; an interrupted event is retried from scratch.

All automated runs use practice mode without checkpoints and intercept completion
to avoid awarding normal completion. Verify and Analyze run behind a cover with the objects hidden and the game sped up by
the `scan-speed` setting (default 1.5, 1 = normal). Exhaustive
analysis of a long run can take many hours. Pause remains available.

## Data

Layout: `levels/<slug>-<levelID>/run-<fingerprint>.cfr` (+ `.bak`). v0.2.x files in the
save dir root are moved there the first time their level is opened. `.cfpack`
(`src/Pack.hpp`) bundles a level's runs: export writes `exports/<folder>.cfpack`,
import reads `imports/*.cfpack`, validates the whole pack (names, sizes, each run with
`cf::read`) before writing, then renames it `.done`. Packs carry no code or sounds.
A shared analysis only applies if level ID, name and content match (same fingerprint)
and the importer runs the same GD/TPS; Play stops on death, timeout or incomplete input playback. Position drift is logged; use Analyze to reverify an imported baseline.

`run-<fingerprint>.cfr` contains a versioned text recording, trace, complete and
verified flags, search radius and results. The fingerprint covers level ID, name
and serialized content, including local levels with ID zero. Sequence order is
the event order within equal ticks. Each save stages a temporary file and keeps
a `.bak` previous version. Old `rec-*.csv` files remain untouched and are not
automatically imported. Record replaces the current session for this level;
the saved predecessor is retained as a backup when the new session is saved.

## Compatibility and validation

GD 2.2081, Geode 5.10.1, Android32/64 and Windows. Classic levels, 240 physics ticks
per second. Platformer and loaded Click Between Frames are rejected. Non-240
command steps and ignore-damage mode are rejected. Other mods that alter inputs,
physics or death handling may invalidate results; disable them during validation.

The standalone tests exercise continuous windows, lower bounds, ordering, dual
streams, persistence and malformed data. Run:

```
g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined tests/timing.cpp -o /tmp/cf-tests
/tmp/cf-tests
```

Before calling this release stable, test on a device: normal and automatic
checkpoints, delete a checkpoint, die while holding jump, dual input, completion,
cancel/reload/resume, and runs with known one-frame and wider windows. Build
success alone does not establish deterministic GD replay. Trace comparison only
covers sampled positions; it is not a full internal-state determinism proof.

## Integration safeguards in 0.4.0

The scheduler scale and object-layer visibility are captured on analysis entry
and restored at pause, stop and exit. Shifted verification invalidates previous
windows; successful verification records the actual end tick and drops releases
that were never dispatched before completion. Pack reads are bounded on disk,
reject duplicate destinations and trailing bytes, and safe writes restore the
backup if the final rename fails. Imports validate every recording before writing;
I/O failure during a multi-run pack may still leave some runs imported, with
previous versions retained as backups.

## 0.5.0 counter and death handling

Rows span every integer width between measured minimum and maximum, including
empty intermediate widths, capped by max-rows with a final N+ bucket. Only the
maximum inherits lowerBound; colors interpolate from red at 1 to green at 10+.
The gameplay and popup counters share this row definition.

Automated collision handling calls the original destroyPlayer and only fails on
an alive-to-dead transition of player1 or an active dual player2. Reset requests
and ignored engine calls no longer count as deaths. This addresses a false-fail
path; the exact Android tick-2 report cannot be reproduced with a recording alone
without the level and runtime/mod state. Device regression must also confirm a
real death still fails and retries the current trial.
