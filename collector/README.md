# Click Frames Collector

Companion dataset collector for Click Frames GD v1.0.14 or newer, Geometry Dash
2.2091 and Geode 6.0.0-alpha.1.

## Storage

- `normal-attempts/<level>/<attempt>/`: every attempt played outside practice and editor test mode.
- `completed-runs/<level>/<attempt>/`: a specialized copy of every normal-mode 100% completion.
- Each attempt has `metadata.json`, `inputs.tsv` and `trajectory.tsv`.
- Each category has `index.tsv` for efficient dataset discovery.

Automated Click Frames analysis and Play showcases are excluded, even though
Play is displayed in normal mode.

The collector stores no account name, API key or device identifier and performs
no network requests. The schema is `CF-COLLECTOR-1`. A trajectory is marked
`samples_truncated` if it reaches the configured mobile-memory limit; its input
events and final outcome are still preserved.

Collected fields include level fingerprint and difficulty metadata, attempt
result and percentage, death object, input transitions, player modes, position,
vertical velocity, speed, gravity, mini, grounded and dual-player state.
