"""Fugue structure detection from output.json provenance data."""

import json
from typing import Dict, List, Optional, Tuple


class FugueDetector:
    """Detects fugue structural elements from provenance data."""

    def __init__(self, data: dict):
        """Initialize with parsed JSON data.

        Args:
            data: Parsed output.json containing tracks with notes and provenance.
        """
        self.data = data
        self.tracks = data.get("tracks", [])

    def detect_subject_entries(self) -> List[dict]:
        """Find all subject entries using provenance.source field."""
        entries = []
        for track in self.tracks:
            for note in track.get("notes", []):
                prov = note.get("provenance", {})
                if prov.get("source") in ("fugue_subject", "fugue_answer"):
                    entry_num = prov.get("entry_number", 0)
                    entries.append(
                        {
                            "tick": note.get("start_ticks", 0),
                            "pitch": note.get("pitch", 0),
                            "voice": track.get("name", ""),
                            "source": prov.get("source"),
                            "entry_number": entry_num,
                        }
                    )
        # Deduplicate by entry_number per voice
        seen = set()
        unique = []
        for entry in entries:
            key = (entry["voice"], entry["entry_number"])
            if key not in seen:
                seen.add(key)
                unique.append(entry)
        return sorted(unique, key=lambda e: e["tick"])

    def detect_exposition(self) -> Optional[dict]:
        """Detect the exposition section (first entry of each voice)."""
        entries = self.detect_subject_entries()
        if not entries:
            return None
        voices_seen = set()
        expo_entries = []
        for entry in entries:
            if entry["voice"] not in voices_seen:
                voices_seen.add(entry["voice"])
                expo_entries.append(entry)
        if not expo_entries:
            return None
        return {
            "start_tick": expo_entries[0]["tick"],
            "end_tick": expo_entries[-1]["tick"] + 1920,  # Approximate
            "voices_entered": len(expo_entries),
            "entries": expo_entries,
        }

    def detect_episodes(self) -> List[dict]:
        """Detect episode sections (gaps between subject entries)."""
        entries = self.detect_subject_entries()
        episodes = []
        for idx in range(len(entries) - 1):
            gap_start = entries[idx]["tick"] + 1920  # After current entry
            gap_end = entries[idx + 1]["tick"]
            if gap_end - gap_start > 960:  # At least half a bar
                episodes.append(
                    {
                        "start_tick": gap_start,
                        "end_tick": gap_end,
                        "duration_ticks": gap_end - gap_start,
                    }
                )
        return episodes

    def detect_stretto(self) -> List[dict]:
        """Detect stretto (overlapping subject entries)."""
        entries = self.detect_subject_entries()
        strettos = []
        for idx in range(len(entries) - 1):
            gap = entries[idx + 1]["tick"] - entries[idx]["tick"]
            if 0 < gap < 1920:  # Less than 1 bar apart
                strettos.append(
                    {
                        "first_entry_tick": entries[idx]["tick"],
                        "second_entry_tick": entries[idx + 1]["tick"],
                        "overlap_ticks": 1920 - gap,
                        "voices": [entries[idx]["voice"], entries[idx + 1]["voice"]],
                    }
                )
        return strettos

    def full_detection(self) -> dict:
        """Run complete fugue structure detection.

        Returns:
            Dictionary with subject_entries, exposition, episodes, and stretto.
        """
        return {
            "subject_entries": self.detect_subject_entries(),
            "exposition": self.detect_exposition(),
            "episodes": self.detect_episodes(),
            "stretto": self.detect_stretto(),
        }
