"""Single-file validation orchestrator."""

from __future__ import annotations

from pathlib import Path
from typing import Dict, List, Optional, Set, Type, Union

from .model import Score
from .loaders import load_json, load_midi
from .rules.base import Category, Rule, RuleResult
from .rules.counterpoint import ALL_COUNTERPOINT_RULES
from .rules.melodic import ALL_MELODIC_RULES
from .rules.overlap import ALL_OVERLAP_RULES
from .rules.dissonance import ALL_DISSONANCE_RULES
from .rules.independence import ALL_INDEPENDENCE_RULES
from .rules.structure import ALL_STRUCTURE_RULES

ALL_RULE_CLASSES: List[Type] = (
    ALL_COUNTERPOINT_RULES
    + ALL_MELODIC_RULES
    + ALL_OVERLAP_RULES
    + ALL_DISSONANCE_RULES
    + ALL_INDEPENDENCE_RULES
    + ALL_STRUCTURE_RULES
)

CATEGORY_MAP: Dict[str, List[Type]] = {
    "counterpoint": ALL_COUNTERPOINT_RULES,
    "melodic": ALL_MELODIC_RULES,
    "overlap": ALL_OVERLAP_RULES,
    "dissonance": ALL_DISSONANCE_RULES,
    "independence": ALL_INDEPENDENCE_RULES,
    "structure": ALL_STRUCTURE_RULES,
}


def load_score(path: Union[str, Path]) -> Score:
    """Auto-detect format and load a Score."""
    p = Path(path)
    if p.suffix.lower() in (".mid", ".midi"):
        return load_midi(p)
    return load_json(p)


def get_rules(
    categories: Optional[Set[str]] = None,
) -> List[Rule]:
    """Instantiate rule objects, optionally filtered by category names."""
    classes = []
    if categories:
        for cat in categories:
            classes.extend(CATEGORY_MAP.get(cat, []))
    else:
        classes = ALL_RULE_CLASSES
    return [cls() for cls in classes]


def validate(
    score: Score,
    categories: Optional[Set[str]] = None,
    bar_range: Optional[tuple] = None,
) -> List[RuleResult]:
    """Run all selected rules on a score.

    Args:
        score: The Score to validate.
        categories: Optional set of category names to run (None = all).
        bar_range: Optional (start_bar, end_bar) to filter violations post-check.

    Returns:
        List of RuleResult from each rule.
    """
    rules = get_rules(categories)
    results = []
    for rule in rules:
        result = rule.check(score)
        if bar_range:
            start_bar, end_bar = bar_range
            result.violations = [
                v for v in result.violations if start_bar <= v.bar <= end_bar
            ]
            result.passed = len(result.violations) == 0
        results.append(result)
    return results


def overall_passed(results: List[RuleResult]) -> bool:
    """True if all rules passed (no CRITICAL/ERROR violations)."""
    for r in results:
        if r.critical_count > 0 or r.error_count > 0:
            return False
    return True
