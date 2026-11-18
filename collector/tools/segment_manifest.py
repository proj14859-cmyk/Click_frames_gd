"""Index clean recorded intervals by level and input mode for sequence training.

Usage: python segment_manifest.py COLLECTOR_SAVE_DIRECTORY
Read source files through each path in segment-manifest.jsonl. Reset recurrent
state at every attempt boundary. The manifest preserves repeated observations.
"""
import csv
import json
import sys
from pathlib import Path

def build(root):
    target = root / "segment-manifest.jsonl"
    temporary = target.with_suffix(".tmp")
    with temporary.open("w", encoding="utf8") as output:
        for path in sorted((root / "normal-attempts").glob("*/*/metadata.json")):
            meta = json.loads(path.read_text(encoding="utf8"))
            if ("from_start_pos" not in meta or meta["samples_truncated"]
                    or meta["platformer"] or not meta["trajectory_samples"]):
                continue
            with (path.parent / "trajectory.tsv").open(encoding="utf8") as stream:
                previous = None
                for frame in csv.DictReader(stream, delimiter="\t"):
                    tick, percent = int(frame["tick"]), float(frame["percent"])
                    bucket = min(3, max(0, int(percent // 25)))
                    if previous and previous["quarter"] != bucket:
                        print(json.dumps(previous), file=output)
                        previous = None
                    if previous is None:
                        previous = dict(level_hash=meta["level_hash"], backend=meta["input_backend"],
                                        attempt_id=meta["attempt_id"], quarter=bucket,
                                        source=str(path.parent.relative_to(root)), first_tick=tick,
                                        first_percent=percent)
                    previous.update(last_tick=tick, last_percent=percent)
                if previous:
                    print(json.dumps(previous), file=output)
    temporary.replace(target)

if __name__ == "__main__":
    build(Path(sys.argv[1]))
