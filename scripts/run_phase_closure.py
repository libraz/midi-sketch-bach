#!/usr/bin/env python3
"""Run Composer phase closure checks outside the C++ test tree.

Generates 20 seeds through bach_cli, scores generated.json with bach-mcp,
checks the model threshold, and optionally checks provenance rule bits.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CLI = REPO_ROOT / "build" / "bin" / "bach_cli"
DEFAULT_INDEX_JS = REPO_ROOT.parent / "bach-mcp" / "dist" / "index.js"

PHASE_DEFAULTS = {
    "Phase3": {"tag": "p3", "threshold": 0.45, "min_pass": 8},
    "Phase35": {"tag": "p3.5", "threshold": 0.45, "min_pass": 8},
    "Phase4": {"tag": "p4", "threshold": 0.55, "min_pass": 8},
    "Phase4Sus": {"tag": "p4sus", "threshold": 0.55, "min_pass": 8},
    "Phase5": {"tag": "p5", "threshold": 0.60, "min_pass": 8},
    "Phase6": {"tag": "p6", "threshold": 0.65, "min_pass": 6},
    "Phase6Episode": {"tag": "p6ep", "threshold": 0.65, "min_pass": 6},
    "Phase6Tonal": {"tag": "p6tonal", "threshold": 0.65, "min_pass": 6},
    "Phase7": {"tag": "p7", "threshold": 0.78, "min_pass": 10},
    "Phase8": {"tag": "p8", "threshold": 0.78, "min_pass": 10},
    "Phase9": {"tag": "p9", "threshold": 0.80, "min_pass": 10},
    "Phase10": {"tag": "p10", "threshold": 0.80, "min_pass": 10},
    "Phase11": {"tag": "p11", "threshold": 0.82, "min_pass": 10},
    "Phase12": {"tag": "p12", "threshold": 0.82, "min_pass": 10},
}

# Phase layout: subject_bars / with_answer / with_third_entry.
# Mirrors HarnessPhaseSpec in src/composer/harness_fixture.cpp.
PHASE_LAYOUT = {
    "Phase3": {"voices": 2, "bars": 8, "subject_bars": 8, "with_answer": False, "with_third_entry": False},
    "Phase35": {"voices": 2, "bars": 4, "subject_bars": 4, "with_answer": False, "with_third_entry": False},
    "Phase4": {"voices": 2, "bars": 8, "subject_bars": 4, "with_answer": True, "with_third_entry": False},
    "Phase4Sus": {"voices": 2, "bars": 8, "subject_bars": 4, "with_answer": True, "with_third_entry": False},
    "Phase5": {"voices": 3, "bars": 12, "subject_bars": 12, "with_answer": False, "with_third_entry": False},
    "Phase6": {"voices": 3, "bars": 16, "subject_bars": 4, "with_answer": True, "with_third_entry": True},
    "Phase6Episode": {"voices": 3, "bars": 16, "subject_bars": 4, "with_answer": True, "with_third_entry": True},
    "Phase6Tonal": {"voices": 3, "bars": 16, "subject_bars": 4, "with_answer": True, "with_third_entry": True, "tonal_answer": True},
    "Phase7": {"voices": 3, "bars": 16, "subject_bars": 4, "with_answer": True, "with_third_entry": True},
    "Phase8": {"voices": 3, "bars": 16, "subject_bars": 4, "with_answer": True, "with_third_entry": True},
    "Phase9": {"voices": 3, "bars": 16, "subject_bars": 4, "with_answer": True, "with_third_entry": True},
    "Phase10": {"voices": 3, "bars": 16, "subject_bars": 4, "with_answer": True, "with_third_entry": True},
    "Phase11": {"voices": 3, "bars": 28, "subject_bars": 4, "with_answer": True, "with_third_entry": True, "development": True},
    "Phase12": {"voices": 3, "bars": 28, "subject_bars": 4, "with_answer": True, "with_third_entry": True},
}

# 5 subject patterns × 16 quarter-note pitches. Mirrors kSubjectPatterns in
# src/composer/harness_fixture.cpp; keep byte-identical with the C++ catalog
# or harness assertions diverge from CLI output.
SUBJECT_PATTERNS: tuple[tuple[int, ...], ...] = (
    (72, 74, 76, 77, 79, 81, 79, 77, 76, 74, 76, 77, 79, 77, 71, 72),
    (84, 83, 84, 79, 77, 76, 77, 79, 81, 79, 77, 76, 74, 76, 71, 72),
    (79, 76, 79, 84, 76, 79, 76, 72, 74, 77, 74, 71, 72, 76, 71, 72),
    (71, 72, 76, 77, 74, 76, 77, 79, 76, 77, 79, 81, 77, 79, 71, 72),
    (76, 77, 79, 81, 79, 77, 76, 74, 72, 74, 76, 77, 79, 77, 71, 72),
)


def normalize_phase(value: str) -> str:
    aliases = {
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
    }
    return aliases.get(value, aliases.get(value.lower(), value))


def fixture_for_seed(seed: int) -> dict[str, Any]:
    return {
        "subj_idx": (seed // 4) % 5,
        "harm_idx": seed % 4,
        "subdivision": "eighth" if (seed % 2) == 1 else "quarter",
    }


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
    if bit_index < 0 or bit_index >= 64:
        raise argparse.ArgumentTypeError(f"rule bit out of range 0..63: {value}")
    return name, bit_index


def provenance_rule_counts(
    provenance_json: Path, required_bits: list[tuple[str, int]]
) -> dict[str, bool]:
    if not required_bits:
        return {}
    with provenance_json.open(encoding="utf-8") as f:
        payload = json.load(f)
    notes = payload.get("notes", [])
    out: dict[str, bool] = {}
    for name, bit in required_bits:
        mask = 1 << bit
        out[name] = any(
            isinstance(note, dict)
            and isinstance(note.get("satisfied_rules"), int)
            and (note["satisfied_rules"] & mask) != 0
            for note in notes
        )
    return out


def expected_carrier_sequences(
    phase: str, fixture: dict[str, Any]
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Predict (voice, intent) -> [(start_tick, pitch)] for a seed fixture.

    Mirrors V0 SubjectCarrier / V1 AnswerCarrier / V2 SubjectCarrier
    re-entry assembly in src/composer/harness_fixture.cpp.
    """
    layout = PHASE_LAYOUT[phase]
    subject_bars = layout["subject_bars"]
    subject_blocks = subject_bars // 4
    subj_a = fixture["subj_idx"]
    ticks_per_bar = 1920
    ticks_per_beat = 480
    out: dict[tuple[int, str], list[tuple[int, int]]] = {}

    v0_seq: list[tuple[int, int]] = []
    for blk in range(subject_blocks):
        pattern = SUBJECT_PATTERNS[(subj_a + blk) % 5]
        for n in range(16):
            tick = (blk * 4 + n // 4) * ticks_per_bar + (n % 4) * ticks_per_beat
            v0_seq.append((tick, pattern[n]))
    if layout.get("development", False):
        # Phase11 restates the subject verbatim in V0 (the stretto leader)
        # at bars 20-23; that span carries intent SubjectCarrier, so it
        # joins the (0, SubjectCarrier) group in extract_carrier_sequences.
        leader_base_bar = 20
        pattern = SUBJECT_PATTERNS[subj_a]
        for n in range(16):
            tick = (leader_base_bar + n // 4) * ticks_per_bar + (n % 4) * ticks_per_beat
            v0_seq.append((tick, pattern[n]))
    out[(0, "SubjectCarrier")] = v0_seq

    if layout["with_answer"]:
        pattern = SUBJECT_PATTERNS[subj_a]
        v1_seq: list[tuple[int, int]] = []
        use_tonal = layout.get("tonal_answer", False)
        tonic_pc = 0  # harness fixes C major / tonic_pc = 0
        dom_pc = 7
        head_length = 4
        for n in range(16):
            tick = (subject_bars + n // 4) * ticks_per_bar + (n % 4) * ticks_per_beat
            base = pattern[n] - 5
            if use_tonal and n < head_length:
                src_pc = pattern[n] % 12
                if src_pc == tonic_pc:
                    target_pc = dom_pc
                elif src_pc == dom_pc:
                    target_pc = tonic_pc
                else:
                    v1_seq.append((tick, base))
                    continue
                # closestPcOctaveTo(target_pc, anchor=base)
                anchor_pc = base % 12
                delta = (target_pc - anchor_pc + 24) % 12
                if delta > 6:
                    delta -= 12
                pitch = max(0, min(127, base + delta))
                v1_seq.append((tick, pitch))
            else:
                v1_seq.append((tick, base))
        out[(1, "AnswerCarrier")] = v1_seq

    if layout["with_third_entry"]:
        pattern = SUBJECT_PATTERNS[subj_a]
        v2_seq: list[tuple[int, int]] = []
        for n in range(16):
            tick = (2 * subject_bars + n // 4) * ticks_per_bar + (n % 4) * ticks_per_beat
            v2_seq.append((tick, pattern[n] - 12))
        out[(2, "SubjectCarrier")] = v2_seq

    return out


def extract_carrier_sequences(
    generated_json: Path, provenance_json: Path
) -> dict[tuple[int, str], list[tuple[int, int]]]:
    """Extract observed (voice, intent) -> sorted [(start_tick, pitch)]."""
    with generated_json.open(encoding="utf-8") as f:
        gen = json.load(f)
    with provenance_json.open(encoding="utf-8") as f:
        prov = json.load(f)
    gen_notes = gen.get("notes", [])
    prov_notes = prov.get("notes", [])
    by_key: dict[tuple[int, str], list[tuple[int, int]]] = {}
    for g, p in zip(gen_notes, prov_notes):
        intent = p.get("voice_intent")
        if intent not in ("SubjectCarrier", "AnswerCarrier"):
            continue
        voice = int(g.get("voice", -1))
        by_key.setdefault((voice, intent), []).append(
            (int(g["start_tick"]), int(g["pitch"]))
        )
    for key in by_key:
        by_key[key].sort()
    return by_key


def first_difference(expected: list[tuple[int, int]],
                     actual: list[tuple[int, int]]) -> dict[str, Any]:
    if len(expected) != len(actual):
        return {"length_mismatch": {"expected": len(expected), "actual": len(actual)}}
    for i, (e, a) in enumerate(zip(expected, actual)):
        if e != a:
            return {"index": i, "expected": e, "actual": a}
    return {}


def structural_check(generated_json: Path, provenance_json: Path,
                     phase: str, fixture: dict[str, Any]) -> dict[str, Any]:
    """Verify carrier voices replay the fixture catalog byte-identically."""
    expected = expected_carrier_sequences(phase, fixture)
    try:
        actual = extract_carrier_sequences(generated_json, provenance_json)
    except (OSError, json.JSONDecodeError) as exc:
        return {"ok": False, "error": str(exc)}

    result: dict[str, Any] = {}
    keys = [
        ((0, "SubjectCarrier"), "subject_ok", "subject_diff"),
        ((1, "AnswerCarrier"), "answer_ok", "answer_diff"),
        ((2, "SubjectCarrier"), "v2_subject_ok", "v2_subject_diff"),
    ]
    all_ok = True
    for key, ok_field, diff_field in keys:
        if key not in expected:
            continue
        exp_seq = expected[key]
        act_seq = actual.get(key, [])
        passed = exp_seq == act_seq
        result[ok_field] = passed
        if not passed:
            result[diff_field] = first_difference(exp_seq, act_seq)
            all_ok = False
    result["ok"] = all_ok
    return result


def output_paths(work_dir: Path, tag: str, seed: int) -> tuple[Path, Path, Path]:
    midi = work_dir / f"bach_harness_{tag}_seed{seed}.mid"
    generated = midi.with_suffix(".json")
    provenance = midi.with_suffix(".provenance.json")
    return midi, generated, provenance


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", default="Phase6", help="Phase3/Phase35/Phase4/Phase5/Phase6")
    parser.add_argument("--seeds", type=int, default=20)
    parser.add_argument("--threshold", type=float)
    parser.add_argument("--min-pass", type=int)
    parser.add_argument("--required-rule-bit", action="append", type=parse_required_rule_bit, default=[])
    parser.add_argument("--required-rule-min", type=int)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--bach-mcp-index", type=Path)
    parser.add_argument("--keep-work", action="store_true")
    args = parser.parse_args()

    phase = normalize_phase(args.phase)
    if phase not in PHASE_DEFAULTS:
        sys.stderr.write(f"unknown phase: {args.phase}\n")
        return 2

    defaults = PHASE_DEFAULTS[phase]
    tag = defaults["tag"]
    threshold = args.threshold if args.threshold is not None else defaults["threshold"]
    min_pass = args.min_pass if args.min_pass is not None else defaults["min_pass"]
    required_rule_min = (
        args.required_rule_min if args.required_rule_min is not None else args.seeds
    )
    report_path = args.out or (REPO_ROOT / "backup" / f"closure_report_{tag}.json")
    work_dir = args.work_dir or (report_path.parent / f"closure_work_{tag}")
    index_js = args.bach_mcp_index or Path(os.environ.get("BACH_MCP_INDEX_JS", DEFAULT_INDEX_JS))

    if not args.cli.exists():
        sys.stderr.write(f"bach_cli missing: {args.cli}\n")
        return 2
    if not index_js.exists():
        sys.stderr.write(f"bach-mcp index.js missing: {index_js}\n")
        return 2

    if work_dir.exists():
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)
    report_path.parent.mkdir(parents=True, exist_ok=True)

    rows: list[dict[str, Any]] = []
    model_pass_count = 0
    composer_ok_count = 0
    structural_ok_count = 0
    rule_hit_counts = {name: 0 for name, _ in args.required_rule_bit}

    for seed in range(args.seeds):
        midi, generated, provenance = output_paths(work_dir, tag, seed)
        fixture = fixture_for_seed(seed)
        proc = run(
            [
                str(args.cli),
                "--composer-phase",
                phase,
                "--seed",
                str(seed),
                "--json",
                "-o",
                str(midi),
            ],
            cwd=REPO_ROOT,
        )
        row: dict[str, Any] = {
            "phase": tag,
            "seed": seed,
            "fixture": fixture,
            "composer_ok": proc.returncode == 0,
            "stdout": proc.stdout,
            "stderr": proc.stderr,
            "generated_json": str(generated),
            "provenance_json": str(provenance),
        }
        if proc.returncode == 0:
            composer_ok_count += 1
            try:
                score = score_generated(index_js, generated)
                prob = model_probability(score)
                row.update(
                    {
                        "evaluator_ok": True,
                        "heuristic": heuristic_score(score),
                        "model_prob": prob,
                        "model_pass": prob >= threshold,
                    }
                )
                if prob >= threshold:
                    model_pass_count += 1
            except RuntimeError as exc:
                row.update({"evaluator_ok": False, "evaluator_error": str(exc)})
            try:
                rule_hits = provenance_rule_counts(provenance, args.required_rule_bit)
                row["required_rule_hits"] = rule_hits
                for name, hit in rule_hits.items():
                    if hit:
                        rule_hit_counts[name] += 1
            except (OSError, json.JSONDecodeError) as exc:
                row["provenance_error"] = str(exc)
            structural = structural_check(generated, provenance, phase, fixture)
            row["structural"] = structural
            if structural.get("ok"):
                structural_ok_count += 1
        rows.append(row)
        sys.stderr.write(
            f"[closure][{tag}][seed={seed}] "
            f"composer_ok={str(row['composer_ok']).lower()} "
            f"model_prob={row.get('model_prob', 0.0):.6f} "
            f"model_pass={str(row.get('model_pass', False)).lower()} "
            f"structural_ok={str(row.get('structural', {}).get('ok', False)).lower()}\n"
        )

    rule_pass = {
        name: count >= required_rule_min for name, count in rule_hit_counts.items()
    }
    passed = (
        composer_ok_count == args.seeds
        and model_pass_count >= min_pass
        and structural_ok_count == composer_ok_count
        and all(rule_pass.values())
    )
    report = {
        "phase": phase,
        "phase_tag": tag,
        "seed_count": args.seeds,
        "threshold": threshold,
        "min_pass": min_pass,
        "composer_ok": composer_ok_count,
        "model_pass": model_pass_count,
        "structural_ok": structural_ok_count,
        "required_rule_min": required_rule_min,
        "required_rule_counts": rule_hit_counts,
        "required_rule_pass": rule_pass,
        "passed": passed,
        "seeds": rows,
    }
    report_path.write_text(json.dumps(report, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    sys.stderr.write(
        f"[closure][{tag}][summary] composer_ok={composer_ok_count}/{args.seeds} "
        f"model_pass={model_pass_count}/{args.seeds} "
        f"structural_ok={structural_ok_count}/{composer_ok_count} report={report_path}\n"
    )

    if not args.keep_work:
        for path in work_dir.glob("*.mid"):
            path.unlink()
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
