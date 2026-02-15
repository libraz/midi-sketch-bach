"""Bach reference data loader.

Loads pre-computed category statistics derived from 270 Bach reference works
via the bach-reference MCP server.
"""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Dict, List

_CATEGORIES_DIR = Path(__file__).parent / "categories"


def load_category(category: str) -> Dict[str, Any]:
    """Load reference data for a category.

    Args:
        category: Category name (e.g., "organ_fugue", "wtc1").

    Returns:
        Dict with 'category', 'work_count', 'scalars', 'distributions' keys.

    Raises:
        FileNotFoundError: If category JSON does not exist.
    """
    path = _CATEGORIES_DIR / f"{category}.json"
    if not path.exists():
        raise FileNotFoundError(f"No reference data for category '{category}': {path}")
    with open(path) as f:
        return json.load(f)


def available_categories() -> List[str]:
    """Return list of available reference category names."""
    return sorted(
        p.stem for p in _CATEGORIES_DIR.glob("*.json")
    )
