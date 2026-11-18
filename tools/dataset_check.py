#!/usr/bin/env python3
"""Offline validator and level-grouped split planner for dataset_entrenamiento.csv."""

from __future__ import annotations

import argparse
from collections import Counter
import csv
from pathlib import Path
import sys

EXPECTED_HEADER = [
    "vehiculo",
    "velocidad_x",
    "velocidad_y",
    "gravedad",
    "tamano",
    "en_suelo",
    "distancia_obs_x",
    "distancia_obs_y",
    "delta_ticks",
    "TARGET_frames",
    "early",
    "late",
    "abierta",
    "radio",
    "digest_nivel",
    "digest_captura",
    "indice_evento",
    "tick",
    "politica",
    "tps",
    "geometria",
    "dual",
    "modo2",
    "y2",
    "vy2",
    "gravedad2",
]

MODE_NAMES = {
    0: "cube",
    1: "ship",
    2: "ball",
    3: "ufo",
    4: "wave",
    5: "robot",
    6: "spider",
    7: "swing",
}

MIN_READY_ROWS = 1000
MIN_READY_LEVELS = 10


def partition_levels(level_counts: Counter[str], val_ratio: float = 0.2) -> tuple[list[str], list[str]]:
    """Deterministic greedy level-grouped train/validation split (never splits a level across sets)."""
    ordered = sorted(level_counts.items(), key=lambda kv: (-kv[1], kv[0]))
    if len(ordered) <= 1:
        return [lvl for lvl, _ in ordered], []

    total_rows = sum(level_counts.values())
    target_val = max(1, round(total_rows * val_ratio))

    train_levels: list[str] = []
    val_levels: list[str] = []
    val_rows = 0

    for idx, (lvl, count) in enumerate(ordered):
        remaining_levels = len(ordered) - idx
        if not train_levels:
            train_levels.append(lvl)
        elif not val_levels and remaining_levels == 1:
            val_levels.append(lvl)
            val_rows += count
        elif val_rows + count <= target_val or (not val_levels and idx == len(ordered) - 1):
            val_levels.append(lvl)
            val_rows += count
        else:
            train_levels.append(lvl)

    return train_levels, val_levels


def check_dataset(csv_path: Path, val_ratio: float = 0.2) -> int:
    if not csv_path.is_file():
        print(f"ERROR: file not found: {csv_path}", file=sys.stderr)
        return 1

    with csv_path.open("r", encoding="utf-8", newline="") as handle:
        reader = csv.reader(handle)
        try:
            header = next(reader)
        except StopIteration:
            print(f"ERROR: empty CSV file: {csv_path}", file=sys.stderr)
            return 1

        if header != EXPECTED_HEADER:
            print("ERROR: unexpected CSV header.", file=sys.stderr)
            print(f"  expected ({len(EXPECTED_HEADER)} cols): {','.join(EXPECTED_HEADER)}", file=sys.stderr)
            print(f"  observed ({len(header)} cols): {','.join(header)}", file=sys.stderr)
            return 1

        by_level: Counter[str] = Counter()
        by_mode: Counter[int] = Counter()
        by_width: Counter[int] = Counter()
        seen_raw_rows: set[tuple[str, ...]] = set()
        seen_events: set[tuple[str, str]] = set()

        total_rows = 0
        closed_rows = 0
        open_rows = 0
        malformed_rows = 0
        exact_duplicates = 0
        event_duplicates = 0
        geometry_rows = 0
        dual_rows = 0

        for line_no, row in enumerate(reader, start=2):
            if not row or all(cell == "" for cell in row):
                continue
            total_rows += 1
            if len(row) != len(EXPECTED_HEADER):
                malformed_rows += 1
                continue

            row_tuple = tuple(row)
            if row_tuple in seen_raw_rows:
                exact_duplicates += 1
            else:
                seen_raw_rows.add(row_tuple)

            try:
                mode = int(row[0])
                width = int(row[9])
                early = int(row[10])
                late = int(row[11])
                is_open = int(row[12])
                level_digest = row[14].strip()
                capture_digest = row[15].strip()
                event_index = row[16].strip()
                tps = int(row[19])
                has_geom = int(row[20])
                is_dual = int(row[21]) if row[21] != "" else 0
            except ValueError:
                malformed_rows += 1
                continue

            if not level_digest or not capture_digest or tps != 240 or width < 1 or early + late + 1 != width:
                malformed_rows += 1
                continue

            event_key = (capture_digest, event_index)
            if event_key in seen_events:
                event_duplicates += 1
            else:
                seen_events.add(event_key)

            if is_open:
                open_rows += 1
                continue

            closed_rows += 1
            by_level[level_digest] += 1
            by_mode[mode] += 1
            by_width[width] += 1
            if has_geom == 1:
                geometry_rows += 1
            if is_dual == 1:
                dual_rows += 1

    unique_levels = len(by_level)
    ready = (
        closed_rows >= MIN_READY_ROWS
        and unique_levels >= MIN_READY_LEVELS
        and malformed_rows == 0
        and open_rows == 0
        and exact_duplicates == 0
        and event_duplicates == 0
    )

    print("=== Click Frames GD - Dataset Check ===")
    print(f"File: {csv_path}")
    print(f"Total rows read:      {total_rows}")
    print(f"Closed valid rows:    {closed_rows} (threshold >= {MIN_READY_ROWS})")
    print(f"Unique levels:        {unique_levels} (threshold >= {MIN_READY_LEVELS})")
    print(f"Rows with geometry:   {geometry_rows} ({(100.0 * geometry_rows / closed_rows) if closed_rows else 0.0:.1f}%)")
    print(f"Dual-mode rows:       {dual_rows} ({(100.0 * dual_rows / closed_rows) if closed_rows else 0.0:.1f}%)")
    print(f"Open rows (unexpected): {open_rows}")
    print(f"Malformed rows:       {malformed_rows}")
    print(f"Exact duplicate rows: {exact_duplicates}")
    print(f"Duplicate events:     {event_duplicates}")
    print(
        f"Training readiness:   {'READY' if ready else 'NOT READY - collect more clean levels/rows or fix duplicates'}"
    )

    print("\n--- Rows by vehicle mode (vehiculo) ---")
    for mode_id in range(8):
        count = by_mode.get(mode_id, 0)
        pct = (100.0 * count / closed_rows) if closed_rows else 0.0
        print(f"  {mode_id} ({MODE_NAMES[mode_id]:6s}): {count:6d} ({pct:5.1f}%)")

    print("\n--- Rows by window width (TARGET_frames) ---")
    for width, count in sorted(by_width.items()):
        pct = (100.0 * count / closed_rows) if closed_rows else 0.0
        print(f"  {width:2d}F: {count:6d} ({pct:5.1f}%)")

    print("\n--- Rows by level (digest_nivel) ---")
    for lvl, count in sorted(by_level.items(), key=lambda kv: (-kv[1], kv[0])):
        pct = (100.0 * count / closed_rows) if closed_rows else 0.0
        print(f"  {lvl}: {count:6d} ({pct:5.1f}%)")

    train_levels, val_levels = partition_levels(by_level, val_ratio=val_ratio)
    train_rows = sum(by_level[lvl] for lvl in train_levels)
    val_rows = sum(by_level[lvl] for lvl in val_levels)
    print(f"\n--- Level-grouped split (target val ratio {val_ratio:.0%}, zero level leakage) ---")
    print(f"  Train: {len(train_levels)} level(s), {train_rows} row(s) -> {', '.join(train_levels) if train_levels else 'none'}")
    print(f"  Val:   {len(val_levels)} level(s), {val_rows} row(s) -> {', '.join(val_levels) if val_levels else 'none'}")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect and validate dataset_entrenamiento.csv before training.")
    parser.add_argument(
        "csv",
        nargs="?",
        default="dataset_entrenamiento.csv",
        help="Path to dataset_entrenamiento.csv (default: ./dataset_entrenamiento.csv)",
    )
    parser.add_argument(
        "--val-ratio",
        type=float,
        default=0.2,
        help="Target validation fraction for level-grouped split (default: 0.2)",
    )
    args = parser.parse_args()
    return check_dataset(Path(args.csv), val_ratio=args.val_ratio)


if __name__ == "__main__":
    raise SystemExit(main())
