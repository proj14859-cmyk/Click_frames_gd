# Click Frames 0.6.0 — experimental

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
