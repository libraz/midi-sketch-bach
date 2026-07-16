#!/usr/bin/env python3
"""Shared core helpers for the Composer closure / listening-packet tooling.

This module owns the repo-root resolution, the ``bach_cli`` / ``bach-mcp``
default paths, the subprocess + scoring helpers, the required-rule-bit parser,
and the :func:`generate_case` convenience that drives one ``bach_cli`` seed and
loads its generated / provenance JSON. Bodies are kept identical with the
historical closure-harness definitions so behaviour does not drift.
"""

from __future__ import annotations

import argparse
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# bachlib lives one directory deeper than the old top-level scripts modules
# (scripts/bachlib/common.py), so the repo root is three parents up.
REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_CLI = REPO_ROOT / "build" / "bin" / "bach_cli"
DEFAULT_INDEX_JS = REPO_ROOT.parent / "bach-mcp" / "dist" / "index.js"


def run(cmd: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False)


def score_generated(index_js: Path, generated_json: Path) -> dict[str, Any]:
    proc = run(["node", str(index_js), "score", str(generated_json)])
    if proc.returncode != 0:
        raise RuntimeError(proc.stdout + proc.stderr)
    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"bach-mcp returned non-JSON output: {proc.stdout}") from exc


def model_probability(score: dict[str, Any]) -> float:
    model = score.get("model_score")
    if isinstance(model, dict):
        value = model.get("probability", 0.0)
        if isinstance(value, (int, float)):
            return float(value)
    return 0.0


def model_probability_v2(score: dict[str, Any]) -> float:
    """KL-divergence model probability (bach-mcp model_score_v2).

    Informational for now: gates and closure thresholds stay anchored to the
    v1 probability scale until they are re-anchored deliberately. Returns -1.0
    when the scorer did not emit a v2 block (older bach-mcp build).
    """
    model = score.get("model_score_v2")
    if isinstance(model, dict):
        value = model.get("probability", -1.0)
        if isinstance(value, (int, float)):
            return float(value)
    return -1.0


def model_probability_v2_length_invariant(score: dict[str, Any]) -> float:
    """Length-invariant KL-model probability (bach-mcp probability_length_invariant).

    The v2 probability is not comparable across piece lengths: the scorer's
    pseudo-count shrinkage suppresses the KL of short pieces toward zero. This
    axis re-bases each component on what real Bach material scores at the same
    event count, so 16-bar and 128-bar runs share one scale. Informational
    only; no gate floor is anchored to it. Returns -1.0 when the scorer did
    not emit the field (older bach-mcp build or pre-calibration model).
    """
    model = score.get("model_score_v2")
    if isinstance(model, dict):
        value = model.get("probability_length_invariant", -1.0)
        if isinstance(value, (int, float)):
            return float(value)
    return -1.0


def heuristic_score(score: dict[str, Any]) -> float:
    value = score.get("score", 0.0)
    return float(value) if isinstance(value, (int, float)) else 0.0


def parse_required_rule_bit(value: str) -> tuple[str, int]:
    if "=" in value:
        name, bit = value.split("=", 1)
    else:
        name, bit = f"bit_{value}", value
    try:
        bit_index = int(bit, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"invalid rule bit: {value}") from exc
    if bit_index < 0 or bit_index >= 128:
        raise argparse.ArgumentTypeError(f"rule bit out of range 0..127: {value}")
    return name, bit_index


def provenance_rule_counts(
    provenance_json: Path, required_bits: list[tuple[str, int]]
) -> dict[str, bool]:
    if not required_bits:
        return {}
    with provenance_json.open(encoding="utf-8") as f:
        payload = json.load(f)
    notes = payload.get("notes", [])
    if not isinstance(notes, list):
        notes = []

    def has_lane_bit(note: object, bit_index: int) -> bool:
        if not isinstance(note, dict):
            return False
        lane_key = "satisfied_rules" if bit_index < 64 else "satisfied_rules_high"
        lane = note.get(lane_key)
        if (
            not isinstance(lane, int)
            or isinstance(lane, bool)
            or lane < 0
            or lane > (1 << 64) - 1
        ):
            return False
        return (lane & (1 << (bit_index % 64))) != 0

    out: dict[str, bool] = {}
    for name, bit in required_bits:
        out[name] = any(has_lane_bit(note, bit) for note in notes)
    return out


@dataclass
class GeneratedCase:
    """Result of generating one ``bach_cli`` seed and loading its JSON.

    @ivar seed The seed that was generated.
    @ivar midi Path to the requested MIDI output (may be removed afterward).
    @ivar generated_json Path to the generated.v1 JSON the CLI emitted.
    @ivar provenance_json Path to the provenance.v1 JSON the CLI emitted.
    @ivar returncode The ``bach_cli`` process exit code.
    @ivar stdout Captured stdout from ``bach_cli``.
    @ivar stderr Captured stderr from ``bach_cli``.
    @ivar generated Parsed generated.v1 payload, or None if absent / unreadable.
    @ivar provenance Parsed provenance.v1 payload, or None if absent / unreadable.
    @ivar error Human-readable failure reason, or None when the case is clean.
    """

    seed: int
    midi: Path
    generated_json: Path
    provenance_json: Path
    returncode: int
    stdout: str
    stderr: str
    generated: dict[str, Any] | None = None
    provenance: dict[str, Any] | None = None
    error: str | None = None

    @property
    def ok(self) -> bool:
        """Whether the CLI exited 0 and both JSON payloads parsed cleanly."""
        return self.returncode == 0 and self.error is None


def generate_case(
    cli: Path,
    phase_or_form: str,
    seed: int,
    work_dir: Path,
    *,
    mode: str = "composer_phase",
    target_bars: int | None = None,
    cwd: Path | None = None,
) -> GeneratedCase:
    """Generate one seed through ``bach_cli`` and load its JSON outputs.

    Consolidates the per-seed invoke + load-generated + load-provenance triad
    used by the closure harness and the listening-packet builder. Two flag
    conventions are supported:

      - ``mode="composer_phase"``: ``--composer-phase <phase> --seed N --json
        -o <midi>``. The CLI writes ``<stem>.json`` (generated.v1) and
        ``<stem>.provenance.json`` next to the MIDI file.
      - ``mode="form"``: ``--form <form> --seed N [--bars N] --generated-json
        -o <midi>``. The CLI writes ``<stem>.generated.json`` and
        ``<stem>.provenance.json`` next to the MIDI file.

    @param cli Path to the ``bach_cli`` binary.
    @param phase_or_form Phase name (composer_phase mode) or form name (form mode).
    @param seed Closure seed (0-based).
    @param work_dir Directory the MIDI / JSON outputs are written into.
    @param mode Either ``"composer_phase"`` or ``"form"``.
    @param target_bars Optional ``--bars`` override (form mode only).
    @param cwd Working directory for the ``bach_cli`` process (defaults REPO_ROOT).
    @return A :class:`GeneratedCase`; check ``.ok`` / ``.error`` before use.
    """
    process_cwd = cwd if cwd is not None else REPO_ROOT
    midi = work_dir / f"bach_case_{phase_or_form}_seed{seed}.mid"
    if mode == "composer_phase":
        generated_json = midi.with_suffix(".json")
        provenance_json = midi.with_suffix(".provenance.json")
        cmd = [
            str(cli),
            "--composer-phase",
            phase_or_form,
            "--seed",
            str(seed),
            "--json",
            "-o",
            str(midi),
        ]
    elif mode == "form":
        # Form mode strips the ``.mid`` suffix and appends the JSON suffix
        # (matching deriveSuffixedJsonPath in cli_main.cpp).
        generated_json = midi.with_suffix(".generated.json")
        provenance_json = midi.with_suffix(".provenance.json")
        cmd = [
            str(cli),
            "--form",
            phase_or_form,
            "--seed",
            str(seed),
            "--generated-json",
            "-o",
            str(midi),
        ]
        if target_bars is not None:
            cmd.extend(["--bars", str(target_bars)])
    else:
        raise ValueError(f"unknown generate_case mode: {mode}")

    proc = run(cmd, cwd=process_cwd)
    case = GeneratedCase(
        seed=seed,
        midi=midi,
        generated_json=generated_json,
        provenance_json=provenance_json,
        returncode=proc.returncode,
        stdout=proc.stdout,
        stderr=proc.stderr,
    )
    if proc.returncode != 0:
        case.error = proc.stderr.strip() or f"bach_cli exited {proc.returncode}"
        return case

    try:
        with generated_json.open(encoding="utf-8") as handle:
            case.generated = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        case.error = f"failed to load generated JSON: {exc}"
        return case
    try:
        with provenance_json.open(encoding="utf-8") as handle:
            case.provenance = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        case.error = f"failed to load provenance JSON: {exc}"
        return case
    return case
