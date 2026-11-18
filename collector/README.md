# Click Frames Collector

Companion dataset collector for Click Frames GD v1.0.18 or newer, Geometry Dash
2.2091 and Geode 6.0.0-alpha.1.

Collection defaults to disabled. In Click Frames, open Settings > Extra options >
Collect normal runs and accept both confirmations. The same button disables it.
Enabling the collector setting alone does not bypass consent.

## Storage

- `normal-attempts/<level>/<attempt>/`: every attempt played outside practice and editor test mode.
- `completed-runs/<level>/<attempt>/`: a specialized copy of every normal-mode 100% completion.
- `startpos-segments/<level>/<attempt>/`: normal-mode attempts started from a Start Pos object.
- Each attempt has `metadata.json`, `inputs.tsv`, `queued-inputs.tsv` and `trajectory.tsv`.
- Each category has `index.tsv` for efficient dataset discovery.

Automated Click Frames analysis and Play showcases are excluded, even though
Play is displayed in normal mode. Attempts with Click Frames Tools Noclip or
Speedhack (speed other than 1.0) are not collected: if Speedhack is active when
the collector is enabled in normal mode, the collector blocks recording and
shows a once-per-session warning explaining why Speedhack cannot be used for
human-timing collection. If Noclip or Speedhack is switched on mid-attempt,
that assisted attempt is discarded without writing any files or index entry.
Legacy attempts recorded by older versions with `result = mode-change` (and
anything not `death`/`completed`) should be ignored when preparing a dataset.

The collector stores no account name, API key or device identifier and performs
no network requests. The schema is `CF-COLLECTOR-1`. A trajectory is marked
`samples_truncated` if it reaches the configured mobile-memory limit; its input
events and final outcome are still preserved.

Collected fields include level fingerprint and difficulty metadata, attempt
result and percentage, death object, input transitions, player modes, position,
vertical velocity, speed, gravity, mini, grounded and dual-player state.
