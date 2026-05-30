#!/usr/bin/env python3
"""Shared helpers for Composer phase closure / listening-packet scripts.

This module is the single source of truth for the per-seed fixture mapping
and the phase-name alias table. Both ``run_phase_closure.py`` and
``build_listening_packet.py`` import from here so the two scripts cannot drift.
"""

from __future__ import annotations

from typing import Any

# Phase-name alias table. Superset of the maps that previously lived inline in
# both run_phase_closure.py and build_listening_packet.py: it covers every key
# from both scripts so importers stay byte-compatible with their old behavior.
_PHASE_ALIASES: dict[str, str] = {
    "3": "Phase3",
    "p3": "Phase3",
    "phase3": "Phase3",
    "3.5": "Phase35",
    "p3.5": "Phase35",
    "p35": "Phase35",
    "phase35": "Phase35",
    "4": "Phase4",
    "p4": "Phase4",
    "phase4": "Phase4",
    "4sus": "Phase4Sus",
    "p4sus": "Phase4Sus",
    "phase4sus": "Phase4Sus",
    "5": "Phase5",
    "p5": "Phase5",
    "phase5": "Phase5",
    "6": "Phase6",
    "p6": "Phase6",
    "phase6": "Phase6",
    "6ep": "Phase6Episode",
    "p6ep": "Phase6Episode",
    "phase6episode": "Phase6Episode",
    "6episode": "Phase6Episode",
    "6tonal": "Phase6Tonal",
    "p6tonal": "Phase6Tonal",
    "phase6tonal": "Phase6Tonal",
    "7": "Phase7",
    "p7": "Phase7",
    "phase7": "Phase7",
    "8": "Phase8",
    "p8": "Phase8",
    "phase8": "Phase8",
    "9": "Phase9",
    "p9": "Phase9",
    "phase9": "Phase9",
    "10": "Phase10",
    "p10": "Phase10",
    "phase10": "Phase10",
    "11": "Phase11",
    "p11": "Phase11",
    "phase11": "Phase11",
    "12": "Phase12",
    "p12": "Phase12",
    "phase12": "Phase12",
    "13": "Phase13",
    "p13": "Phase13",
    "phase13": "Phase13",
    "14": "Phase14",
    "p14": "Phase14",
    "phase14": "Phase14",
}


def normalize_phase(value: str) -> str:
    """Map a user-supplied phase token to its canonical ``PhaseN`` name.

    @param value Raw phase token (e.g. "p14", "PHASE14", "Phase14").
    @return Canonical phase name, or the original value if no alias matches.
    """
    return _PHASE_ALIASES.get(value, _PHASE_ALIASES.get(value.lower(), value))


def fixture_for_seed(seed: int) -> dict[str, Any]:
    """Derive the harness fixture selection for a closure seed.

    Mirrors the seed->fixture mapping the C++ harness uses so the Python
    structural predictor selects the same subject/harmony/subdivision.

    @param seed Closure seed (0-based).
    @return Dict with ``subj_idx`` (0-4), ``harm_idx`` (0-3) and
            ``subdivision`` ("quarter" for even seeds, "eighth" for odd).
    """
    return {
        "subj_idx": (seed // 4) % 5,
        "harm_idx": seed % 4,
        "subdivision": "eighth" if (seed % 2) == 1 else "quarter",
    }
