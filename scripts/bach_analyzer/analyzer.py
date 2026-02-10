"""Counterpoint quality analyzer for Bach MIDI output."""

import json
import sys
from pathlib import Path
from typing import Dict, List, Optional, Tuple


class CounterpointAnalyzer:
    """Analyzes counterpoint quality from output.json data."""

    def __init__(self, data: dict):
        """Initialize with parsed JSON data.

        Args:
            data: Parsed output.json containing tracks with notes and provenance.
        """
        self.data = data
        self.tracks = data.get("tracks", [])
        self.notes = self._extract_all_notes()

    def _extract_all_notes(self) -> List[dict]:
        """Extract all notes from all tracks."""
        all_notes = []
        for track in self.tracks:
            for note in track.get("notes", []):
                all_notes.append(note)
        return sorted(all_notes, key=lambda n: n.get("start_ticks", 0))

    def count_parallel_fifths(self) -> int:
        """Count parallel perfect fifth violations between voice pairs."""
        return self._count_parallel_interval(7)

    def count_parallel_octaves(self) -> int:
        """Count parallel octave violations between voice pairs."""
        return self._count_parallel_interval(0)  # 0 mod 12 = unison/octave

    def _count_parallel_interval(self, target_interval: int) -> int:
        """Count parallel motion to a target interval (mod 12)."""
        voices = self._group_by_voice()
        count = 0
        voice_ids = sorted(voices.keys())
        for idx in range(len(voice_ids)):
            for jdx in range(idx + 1, len(voice_ids)):
                count += self._check_parallel_between(
                    voices[voice_ids[idx]], voices[voice_ids[jdx]], target_interval
                )
        return count

    def _group_by_voice(self) -> Dict[str, List[dict]]:
        """Group notes by voice/track name."""
        voices = {}
        for track in self.tracks:
            name = track.get("name", "unknown")
            voices[name] = sorted(
                track.get("notes", []), key=lambda n: n.get("start_ticks", 0)
            )
        return voices

    def _check_parallel_between(
        self, voice_a: List[dict], voice_b: List[dict], target: int
    ) -> int:
        """Check for parallel motion to target interval between two voices."""
        count = 0
        # Find simultaneous note pairs at each beat
        beats_a = {n["start_ticks"]: n for n in voice_a}
        beats_b = {n["start_ticks"]: n for n in voice_b}
        common_ticks = sorted(set(beats_a.keys()) & set(beats_b.keys()))

        for kdx in range(len(common_ticks) - 1):
            tick_cur, tick_next = common_ticks[kdx], common_ticks[kdx + 1]
            if (
                tick_cur in beats_a
                and tick_cur in beats_b
                and tick_next in beats_a
                and tick_next in beats_b
            ):
                interval1 = abs(beats_a[tick_cur]["pitch"] - beats_b[tick_cur]["pitch"]) % 12
                interval2 = abs(beats_a[tick_next]["pitch"] - beats_b[tick_next]["pitch"]) % 12
                if interval1 == target and interval2 == target:
                    dir_a = beats_a[tick_next]["pitch"] - beats_a[tick_cur]["pitch"]
                    dir_b = beats_b[tick_next]["pitch"] - beats_b[tick_cur]["pitch"]
                    if dir_a != 0 and dir_b != 0 and (dir_a > 0) == (dir_b > 0):
                        count += 1
        return count

    def count_voice_crossings(self) -> int:
        """Count voice crossing violations."""
        voices = self._group_by_voice()
        voice_ids = sorted(voices.keys())
        count = 0
        for idx in range(len(voice_ids) - 1):
            upper = voices[voice_ids[idx]]
            lower = voices[voice_ids[idx + 1]]
            beats_u = {n["start_ticks"]: n for n in upper}
            beats_l = {n["start_ticks"]: n for n in lower}
            for tick in set(beats_u.keys()) & set(beats_l.keys()):
                if beats_u[tick]["pitch"] < beats_l[tick]["pitch"]:
                    count += 1
        return count

    def voice_independence_score(self) -> Dict[str, float]:
        """Calculate voice independence metrics."""
        voices = self._group_by_voice()
        voice_ids = sorted(voices.keys())
        if len(voice_ids) < 2:
            return {"rhythm": 0.0, "contour": 0.0, "register": 0.0, "composite": 0.0}

        rhythm_scores = []
        contour_scores = []
        register_scores = []

        for idx in range(len(voice_ids)):
            for jdx in range(idx + 1, len(voice_ids)):
                voice_a, voice_b = voices[voice_ids[idx]], voices[voice_ids[jdx]]
                rhythm_scores.append(self._rhythm_independence(voice_a, voice_b))
                contour_scores.append(self._contour_independence(voice_a, voice_b))
                register_scores.append(self._register_separation(voice_a, voice_b))

        rhythm = min(rhythm_scores) if rhythm_scores else 0.0
        contour = min(contour_scores) if contour_scores else 0.0
        register = min(register_scores) if register_scores else 0.0
        composite = rhythm * 0.4 + contour * 0.3 + register * 0.3

        return {
            "rhythm": rhythm,
            "contour": contour,
            "register": register,
            "composite": composite,
        }

    def _rhythm_independence(self, voice_a: List[dict], voice_b: List[dict]) -> float:
        """Rhythm independence: proportion of non-simultaneous attacks."""
        ticks_a = {n["start_ticks"] for n in voice_a}
        ticks_b = {n["start_ticks"] for n in voice_b}
        if not ticks_a and not ticks_b:
            return 0.0
        total = len(ticks_a | ticks_b)
        simultaneous = len(ticks_a & ticks_b)
        return 1.0 - (simultaneous / total) if total > 0 else 0.0

    def _contour_independence(self, voice_a: List[dict], voice_b: List[dict]) -> float:
        """Contour independence: opposite melodic directions."""
        if len(voice_a) < 2 or len(voice_b) < 2:
            return 0.0
        dirs_a = [
            1
            if voice_a[idx + 1]["pitch"] > voice_a[idx]["pitch"]
            else (-1 if voice_a[idx + 1]["pitch"] < voice_a[idx]["pitch"] else 0)
            for idx in range(len(voice_a) - 1)
        ]
        dirs_b = [
            1
            if voice_b[idx + 1]["pitch"] > voice_b[idx]["pitch"]
            else (-1 if voice_b[idx + 1]["pitch"] < voice_b[idx]["pitch"] else 0)
            for idx in range(len(voice_b) - 1)
        ]
        min_len = min(len(dirs_a), len(dirs_b))
        if min_len == 0:
            return 0.0
        opposite = sum(1 for idx in range(min_len) if dirs_a[idx] * dirs_b[idx] < 0)
        return opposite / min_len

    def _register_separation(self, voice_a: List[dict], voice_b: List[dict]) -> float:
        """Register separation: pitch range non-overlap."""
        if not voice_a or not voice_b:
            return 0.0
        min_a = min(n["pitch"] for n in voice_a)
        max_a = max(n["pitch"] for n in voice_a)
        min_b = min(n["pitch"] for n in voice_b)
        max_b = max(n["pitch"] for n in voice_b)
        overlap = max(0, min(max_a, max_b) - max(min_a, min_b))
        total_range = max(max_a, max_b) - min(min_a, min_b)
        return 1.0 - (overlap / total_range) if total_range > 0 else 0.0

    def full_analysis(self) -> dict:
        """Run complete analysis and return results."""
        independence = self.voice_independence_score()
        return {
            "parallel_fifths": self.count_parallel_fifths(),
            "parallel_octaves": self.count_parallel_octaves(),
            "voice_crossings": self.count_voice_crossings(),
            "voice_independence": independence,
            "num_tracks": len(self.tracks),
            "total_notes": len(self.notes),
        }


def analyze_file(filepath: str, track_filter: Optional[str] = None) -> dict:
    """Analyze an output.json file.

    Args:
        filepath: Path to output.json.
        track_filter: Optional track name to filter analysis.

    Returns:
        Analysis results dictionary.
    """
    with open(filepath) as file_handle:
        data = json.load(file_handle)

    if track_filter:
        data["tracks"] = [
            t for t in data.get("tracks", []) if t.get("name") == track_filter
        ]

    analyzer = CounterpointAnalyzer(data)
    return analyzer.full_analysis()


def main():
    """CLI entry point."""
    import argparse

    parser = argparse.ArgumentParser(description="Bach counterpoint analyzer")
    parser.add_argument("input", help="Path to output.json")
    parser.add_argument("--track", help="Filter by track name")
    parser.add_argument("--bar-range", help="Bar range (e.g. 10-20)")
    parser.add_argument("--json", action="store_true", help="JSON output")
    args = parser.parse_args()

    results = analyze_file(args.input, args.track)

    if args.json:
        print(json.dumps(results, indent=2))
    else:
        from . import formatter

        formatter.print_analysis(results)


if __name__ == "__main__":
    main()
