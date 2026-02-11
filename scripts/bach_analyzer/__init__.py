"""Bach MIDI Generator - Comprehensive Validation Tool.

Usage:
    python -m bach_analyzer validate output.json
    python -m bach_analyzer validate output.mid
    python -m bach_analyzer batch --seeds 1-50 --form fugue
"""

from .model import Note, NoteSource, Score, Track
from .runner import load_score, overall_passed, validate

__all__ = [
    "Note",
    "NoteSource",
    "Score",
    "Track",
    "load_score",
    "overall_passed",
    "validate",
]
