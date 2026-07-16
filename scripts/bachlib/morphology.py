#!/usr/bin/env python3
"""Piano-roll morphology bridge to the canonical bach-mcp analyzer."""

from __future__ import annotations

import argparse
import json
import sys
from collections.abc import Callable
from pathlib import Path
from subprocess import CompletedProcess
from typing import Any

from bachlib.common import DEFAULT_INDEX_JS, run

Runner = Callable[[list[str]], CompletedProcess[str]]


def morphology_command(
    index_js: Path,
    generated_json: Path,
    reference: str | None = None,
    bitmap_prefix: Path | None = None,
    provenance: Path | None = None,
    window_beats: float | None = None,
    window_hop_beats: float | None = None,
) -> list[str]:
    """Build the stable bach-mcp morphology CLI invocation."""
    command = ["node", str(index_js), "morphology", str(generated_json)]
    if reference is not None:
        command.extend(["--reference", reference])
    if provenance is not None:
        command.extend(["--provenance", str(provenance)])
    if window_beats is not None:
        command.extend(["--window-beats", str(window_beats)])
    if window_hop_beats is not None:
        command.extend(["--window-hop-beats", str(window_hop_beats)])
    if bitmap_prefix is not None:
        command.extend(["--bitmap-prefix", str(bitmap_prefix)])
    return command


def analyze_morphology(
    index_js: Path,
    generated_json: Path,
    reference: str | None = None,
    bitmap_prefix: Path | None = None,
    provenance: Path | None = None,
    window_beats: float | None = None,
    window_hop_beats: float | None = None,
    *,
    runner: Runner = run,
) -> dict[str, Any]:
    """Run bach-mcp and require a versioned morphology report."""
    proc = runner(
        morphology_command(
            index_js,
            generated_json,
            reference,
            bitmap_prefix,
            provenance,
            window_beats,
            window_hop_beats,
        )
    )
    if proc.returncode != 0:
        detail = (proc.stdout + proc.stderr).strip()
        raise RuntimeError(detail or f"bach-mcp morphology exited {proc.returncode}")
    try:
        report = json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"bach-mcp returned non-JSON morphology output: {proc.stdout}") from exc
    if not isinstance(report, dict) or report.get("schema_version") != "bach-morphology-report.v1":
        raise RuntimeError("bach-mcp returned an unsupported morphology report")
    candidate = report.get("candidate")
    if not isinstance(candidate, dict) or candidate.get("schema_version") != (
        "bach-morphology-profile.v1"
    ):
        raise RuntimeError("morphology report is missing the candidate profile")
    return report


def run_command(args: argparse.Namespace) -> int:
    """Execute the morphology subcommand and write its JSON report."""
    try:
        report = analyze_morphology(
            args.index_js,
            args.generated_json,
            args.reference,
            args.bitmap_prefix,
            args.provenance,
            args.window_beats,
            args.window_hop_beats,
        )
    except (OSError, RuntimeError) as exc:
        print(f"morphology: {exc}", file=sys.stderr)
        return 2

    rendered = json.dumps(report, ensure_ascii=False, indent=2) + "\n"
    if args.out is not None:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0


def register(subparsers: argparse._SubParsersAction) -> None:
    """Register ``bach_tools.py morphology``."""
    parser = subparsers.add_parser(
        "morphology",
        help="compare generated.v1 piano-roll morphology with an optional Bach reference",
        description=(
            "Rasterize generated.v1 on a 32nd-note grid through bach-mcp, return morphology "
            "diagnostics, optionally compare a reference work, and optionally emit PBM/PGM planes."
        ),
    )
    parser.add_argument("generated_json", type=Path, help="generated.v1 JSON to analyze")
    parser.add_argument(
        "--reference",
        help="optional bach-mcp reference work id, for example BWV565",
    )
    parser.add_argument(
        "--bitmap-prefix",
        type=Path,
        help="optional path prefix for occupancy/onset PBM and coverage PGM artifacts",
    )
    parser.add_argument(
        "--provenance",
        type=Path,
        help="optional index-parallel provenance.v1 for local span/intent attribution",
    )
    parser.add_argument(
        "--window-beats",
        type=float,
        help="local comparison window in quarter-note beats (default: 16)",
    )
    parser.add_argument(
        "--window-hop-beats",
        type=float,
        help="distance between local window starts in beats (default: 8)",
    )
    parser.add_argument(
        "--index-js",
        type=Path,
        default=DEFAULT_INDEX_JS,
        help=f"bach-mcp built CLI (default: {DEFAULT_INDEX_JS})",
    )
    parser.add_argument("--out", type=Path, help="optional JSON report output path")
    parser.set_defaults(func=run_command)
