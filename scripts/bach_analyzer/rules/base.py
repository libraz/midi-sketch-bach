"""Rule protocol, violation types, and severity/category enums."""

from __future__ import annotations

from dataclasses import dataclass, field
from enum import Enum
from typing import List, Optional, Protocol, runtime_checkable

from ..model import NoteSource, Score


class Severity(Enum):
    """Violation severity level."""
    CRITICAL = "CRITICAL"
    ERROR = "ERROR"
    WARNING = "WARNING"
    INFO = "INFO"


class Category(Enum):
    """Rule category."""
    COUNTERPOINT = "counterpoint"
    MELODIC = "melodic"
    OVERLAP = "overlap"
    DISSONANCE = "dissonance"
    STRUCTURE = "structure"
    INDEPENDENCE = "independence"
    CADENCE = "cadence"
    ARPEGGIO = "arpeggio"
    ORNAMENT = "ornament"


@dataclass
class Violation:
    """A single rule violation."""
    rule_name: str
    category: Category
    severity: Severity
    bar: int = 0
    beat: int = 0
    tick: int = 0
    voice_a: str = ""
    voice_b: str = ""
    description: str = ""
    source: Optional[NoteSource] = None
    # Diagnostic fields (Phase 5-A)
    source_b: Optional[NoteSource] = None
    interval_semitones: Optional[int] = None  # simple interval [0-11]
    modified_by_a: int = 0  # NoteModifiedBy bitfield
    modified_by_b: int = 0  # NoteModifiedBy bitfield

    @property
    def location(self) -> str:
        """Human-readable location string."""
        loc = f"bar {self.bar}"
        if self.beat:
            loc += f" beat {self.beat}"
        voices = self.voice_a
        if self.voice_b:
            voices += f"<>{self.voice_b}"
        if voices:
            loc += f" {voices}"
        return loc


@dataclass
class RuleResult:
    """Result of applying a single rule."""
    rule_name: str
    category: Category
    passed: bool
    violations: List[Violation] = field(default_factory=list)
    info: str = ""

    @property
    def violation_count(self) -> int:
        return len(self.violations)

    @property
    def critical_count(self) -> int:
        return sum(1 for v in self.violations if v.severity == Severity.CRITICAL)

    @property
    def error_count(self) -> int:
        return sum(1 for v in self.violations if v.severity == Severity.ERROR)

    @property
    def warning_count(self) -> int:
        return sum(1 for v in self.violations if v.severity == Severity.WARNING)


@runtime_checkable
class Rule(Protocol):
    """Protocol for a validation rule."""

    @property
    def name(self) -> str: ...

    @property
    def category(self) -> Category: ...

    def check(self, score: Score) -> RuleResult: ...
