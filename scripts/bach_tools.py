#!/usr/bin/env python3
"""Unified CLI for the midi-sketch-bach Python tooling.

Every validation, reporting, extraction, and audio tool lives in the
:mod:`bachlib` package and registers exactly one subcommand here, so
``python3 scripts/bach_tools.py --help`` is the complete catalog.

Typical commands:
    python3 scripts/bach_tools.py closure --phase FugueComplete --seeds 20
    python3 scripts/bach_tools.py validate generated.json
    python3 scripts/bach_tools.py texture-gate --forms fugue
    python3 scripts/bach_tools.py coverage
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from bachlib import (  # noqa: E402
    audio,
    closure,
    completion,
    coverage,
    extract_entry_plan,
    extract_melodic,
    extract_subject,
    extract_texture,
    gen_mirror,
    ornament_report,
    review,
    schema,
    subject_stats,
    subject_synth,
    texture_gate,
)

# Registration order is the display order of `--help`.
_COMMAND_MODULES = (
    closure,
    audio,  # registers both `render` and `listening`
    texture_gate,
    completion,
    coverage,
    schema,
    ornament_report,
    extract_subject,
    subject_stats,
    subject_synth,
    extract_entry_plan,
    extract_texture,
    extract_melodic,
    gen_mirror,
    review,
)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="bach_tools.py",
        description="midi-sketch-bach Python tooling (closure harness, gates, "
        "reports, corpus extraction, audio rendering).",
    )
    subparsers = parser.add_subparsers(dest="command", required=True, metavar="<command>")
    for module in _COMMAND_MODULES:
        module.register(subparsers)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
