# Click Frames 0.1.0 — experimental

## Flow

Pause > Frames > Record starts a new practice run from zero. Checkpoints keep
the input prefix, trace prefix and held buttons; a retry discards the failed
suffix. Finishing starts an automatic baseline replay. A death, a trajectory
deviation greater than 0.5 game units at a matching sampled tick, or a timeout
stops verification. Only a verified run can be analyzed.

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
to avoid awarding normal completion. Trials are not accelerated: exhaustive
analysis of a long run can take many hours. Pause remains available.

## Data

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
