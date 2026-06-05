#!/usr/bin/env python3
"""Review Phase A stream-segregation cues on bach-mcp reference JSON files."""

from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path

TICKS_PER_BEAT = 480
MIN_TRANSITION_INTERVAL = 6
NEIGHBORHOOD_RADIUS = 4
SIXTEENTH_IOI = TICKS_PER_BEAT // 4
EIGHTH_IOI = TICKS_PER_BEAT // 2


def stream_threshold(ioi: int) -> int:
    if ioi <= SIXTEENTH_IOI:
        return 5
    if ioi <= EIGHTH_IOI:
        return 7
    return 128


def sign(value: int) -> int:
    return 1 if value > 0 else -1 if value < 0 else 0


def median_abs_interval(intervals: list[int], current: int) -> int:
    begin = max(0, current - NEIGHBORHOOD_RADIUS)
    end = min(len(intervals), current + NEIGHBORHOOD_RADIUS + 1)
    window = [abs(intervals[i]) for i in range(begin, end) if i != current]
    if not window:
        return abs(intervals[current])
    return int(statistics.median(window))


def analyze(notes: list[dict]) -> dict:
    notes = sorted(notes, key=lambda note: note["onset"])
    if len(notes) < 3:
        return {"detected_stream_count": 1, "transition_note_indices": [], "separation": 0}

    intervals = [notes[i]["pitch"] - notes[i - 1]["pitch"] for i in range(1, len(notes))]
    transitions: list[int] = []
    low_max = -1
    high_min = 128
    for i in range(1, len(intervals)):
        current = intervals[i]
        abs_current = abs(current)
        large = abs_current >= MIN_TRANSITION_INTERVAL
        reversal = sign(intervals[i - 1]) != 0 and sign(current) != 0 and sign(intervals[i - 1]) != sign(current)
        contrast = abs_current >= 2 * max(1, median_abs_interval(intervals, i))
        ioi = notes[i + 1]["onset"] - notes[i]["onset"]
        strength = abs_current >= stream_threshold(ioi)
        if not (large and reversal and contrast and strength):
            continue
        transitions.append(i + 1)
        a = notes[i]["pitch"]
        b = notes[i + 1]["pitch"]
        low_max = max(low_max, min(a, b))
        high_min = min(high_min, max(a, b))

    return {
        "detected_stream_count": 2 if transitions else 1,
        "transition_note_indices": transitions,
        "separation": max(0, high_min - low_max) if transitions else 0,
    }


def iter_track_notes(doc: dict) -> list[tuple[str, list[dict]]]:
    tracks = doc.get("tracks", [])
    out: list[tuple[str, list[dict]]] = []
    for index, track in enumerate(tracks):
        notes = track.get("notes", [])
        if notes:
            out.append((track.get("role", f"track_{index}"), notes))
    return out


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--corpus-dir",
        type=Path,
        default=Path("~/Projects/bach-mcp/data/reference").expanduser(),
    )
    parser.add_argument("bwv", nargs="*", default=["BWV1007_1", "BWV1004_5"])
    args = parser.parse_args()

    for bwv in args.bwv:
        path = args.corpus_dir / f"{bwv}.json"
        with path.open() as handle:
            doc = json.load(handle)
        print(f"{bwv} category={doc.get('category')} track_type={doc.get('track_type')}")
        for role, notes in iter_track_notes(doc):
            result = analyze(notes)
            print(
                f"  {role}: streams={result['detected_stream_count']} "
                f"transitions={len(result['transition_note_indices'])} "
                f"separation={result['separation']} "
                f"first={result['transition_note_indices'][:8]}"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
