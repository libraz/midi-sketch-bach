#!/usr/bin/env python3
"""Closure engine: gate combination, per-seed driver, and the CLI.

This module owns the closure run: it resolves per-phase defaults, enforces the
per-phase required-rule-bit guards, drives ``bach_cli`` across seeds (optionally
in parallel), scores each seed, runs the structural carrier check, aggregates
the gate counters, and writes the closure report. It imports the seed/phase
tables from :mod:`bachlib.phases`, the mirror constants from
:mod:`bachlib.mirror`, the structural predictors from :mod:`bachlib.predictors`,
and the scoring / subprocess helpers from :mod:`bachlib.common`.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any

from bachlib.common import (
    DEFAULT_CLI,
    DEFAULT_INDEX_JS,
    REPO_ROOT,
    heuristic_score,
    model_probability,
    parse_required_rule_bit,
    provenance_rule_counts,
    run as run_command,
    score_generated,
)
from bachlib.mirror import (
    FUGUE_COMPLETE_REQUIRED_BIT_COUNT,
    CELLO_PRELUDE_REQUIRED_BITS,
    CHACONNE_REQUIRED_BITS,
    ORGAN_PRELUDE_REQUIRED_BITS,
    ORGAN_TOCCATA_REQUIRED_BITS,
    CHORALE_PRELUDE_REQUIRED_BITS,
    PASSACAGLIA_REQUIRED_BITS,
    TRIO_SONATA_REQUIRED_BITS,
    FANTASIA_REQUIRED_BITS,
    KEYBOARD_SUITE_REQUIRED_BITS,
    PRELUDE_AND_FUGUE_REQUIRED_BITS,
    GOLDBERG_VARIATIONS_REQUIRED_BITS,
)
from bachlib.phases import (
    PHASE_DEFAULTS,
    PHASE_LAYOUT,
    fixture_for_seed,
    normalize_phase,
)
from bachlib.predictors import structural_check

def prepare_work_dir(work_dir: Path) -> None:
    """Create or reset a closure work directory.

    The filesystem root, the home directory, the repository root and its parent
    are always rejected, as is a symlink, so a mistyped --work-dir cannot point
    the reset at a broad target. Any other existing directory is cleared: each
    run starts from an empty directory so a stale artifact cannot be scored.
    """
    resolved = work_dir.resolve()
    forbidden = {
        Path("/").resolve(),
        Path.home().resolve(),
        REPO_ROOT.resolve(),
        REPO_ROOT.parent.resolve(),
    }
    if resolved in forbidden or work_dir.is_symlink():
        raise ValueError(f"unsafe closure work directory: {work_dir}")
    if work_dir.exists():
        if not work_dir.is_dir():
            raise ValueError(f"closure work directory is not a directory: {work_dir}")
        shutil.rmtree(work_dir)
    work_dir.mkdir(parents=True)


def output_paths(work_dir: Path, tag: str, seed: int) -> tuple[Path, Path, Path]:
    midi = work_dir / f"bach_harness_{tag}_seed{seed}.mid"
    generated = midi.with_suffix(".json")
    provenance = midi.with_suffix(".provenance.json")
    return midi, generated, provenance


def compute_passed(
    *,
    seed_count: int,
    composer_ok_count: int,
    model_pass_count: int,
    min_pass: int,
    structural_ok_count: int,
    rule_pass: dict[str, bool],
    all_bits_pass: bool,
    evaluator_error_count: int,
) -> bool:
    """Combine the closure gates into the final pass/fail decision.

    Pure function so the gate logic can be unit-tested without running the
    20-seed pipeline.

    @param seed_count Number of seeds attempted.
    @param composer_ok_count Seeds where bach_cli exited 0.
    @param model_pass_count Seeds at/over the model threshold.
    @param min_pass Minimum number of seeds that must reach the model threshold.
    @param structural_ok_count Seeds whose carrier sequences matched.
    @param rule_pass Per-bit name -> whether it hit in enough seeds.
    @param all_bits_pass Whether the all-bits-fire seed count met --all-bits-min.
    @param evaluator_error_count Seeds whose scorer crashed / returned non-JSON.
    @return True only if every gate passes (a crashed scorer fails the run).
    """
    return (
        composer_ok_count == seed_count
        and model_pass_count >= min_pass
        and structural_ok_count == composer_ok_count
        and all(rule_pass.values())
        and all_bits_pass
        # A crashed / non-JSON scorer must not silently look clean.
        and evaluator_error_count == 0
    )


def _check_required_bits(phase: str, required_rule_bit: list[tuple[str, int]]) -> int | None:
    """Enforce the per-phase required-rule-bit guards.

    Each milestone phase declares a fixed set of RuleBits that must be asserted,
    otherwise the bit checks would be trivially bypassable. Refuses to run unless
    the masks are supplied.

    @param phase Canonical phase name.
    @param required_rule_bit Parsed (name, bit) pairs supplied on the CLI.
    @return An exit code (2) to abort with, or None when the guard passes.
    """
    names = [name for name, _ in required_rule_bit]
    bits = [bit for _, bit in required_rule_bit]
    duplicate_names = sorted({name for name in names if names.count(name) > 1})
    duplicate_bits = sorted({bit for bit in bits if bits.count(bit) > 1})
    if duplicate_names or duplicate_bits:
        sys.stderr.write(
            "duplicate --required-rule-bit entries are not allowed; "
            f"duplicate names={duplicate_names}, duplicate bits={duplicate_bits}.\n"
        )
        return 2

    supplied = set(bits)

    def missing(bits: tuple[int, ...]) -> list[int]:
        return [b for b in bits if b not in supplied]

    if phase == "FugueComplete":
        required = tuple(range(FUGUE_COMPLETE_REQUIRED_BIT_COUNT))
        miss = missing(required)
        if miss:
            sys.stderr.write(
                "FugueComplete closure requires --required-rule-bit masks for "
                f"every organ-fugue RuleBit 0..{FUGUE_COMPLETE_REQUIRED_BIT_COUNT - 1}; "
                f"missing {miss}.\n"
            )
            return 2

    if phase == "CelloPrelude":
        miss = missing(CELLO_PRELUDE_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "CelloPrelude closure requires --required-rule-bit masks for both "
                f"Flow bits {list(CELLO_PRELUDE_REQUIRED_BITS)} "
                f"(ArpeggioFlowActive=47, ImplicitVoiceTracked=48); missing {miss}.\n"
            )
            return 2
    if phase == "Chaconne":
        miss = missing(CHACONNE_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "Chaconne closure requires --required-rule-bit masks for all three "
                f"Arch bits {list(CHACONNE_REQUIRED_BITS)} "
                "(GroundBassReplayed=49, VariationRoleApplied=50, "
                f"TextureDensityShift=51); missing {miss}.\n"
            )
            return 2
    if phase == "OrganPrelude":
        miss = missing(ORGAN_PRELUDE_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "OrganPrelude closure requires --required-rule-bit masks for all three "
                f"Prelude bits {list(ORGAN_PRELUDE_REQUIRED_BITS)} "
                "(FigurationCommitted=52, CadenzaApplied=53, "
                f"PedalPreparation=54); missing {miss}.\n"
            )
            return 2
    if phase == "OrganToccata":
        miss = missing(ORGAN_TOCCATA_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "OrganToccata closure requires --required-rule-bit masks for both "
                f"Toccata bits {list(ORGAN_TOCCATA_REQUIRED_BITS)} "
                "(ToccataArchetypeApplied=55, SectionTransition=56); "
                f"missing {miss}.\n"
            )
            return 2
    if phase == "ChoralePrelude":
        miss = missing(CHORALE_PRELUDE_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "ChoralePrelude closure requires --required-rule-bit masks for both "
                f"Chorale-Prelude bits {list(CHORALE_PRELUDE_REQUIRED_BITS)} "
                "(CantusFirmusReplayed=57, CFEmbellishmentApplied=58); "
                f"missing {miss}.\n"
            )
            return 2
    if phase == "Passacaglia":
        miss = missing(PASSACAGLIA_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "Passacaglia closure requires --required-rule-bit masks for all three "
                f"Passacaglia bits {list(PASSACAGLIA_REQUIRED_BITS)} "
                "(PassacagliaGroundReplayed=59, VariationApplied=60, "
                f"ClimaxPlaced=61); missing {miss}.\n"
            )
            return 2
    if phase == "TrioSonata":
        miss = missing(TRIO_SONATA_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "TrioSonata closure requires the --required-rule-bit mask for the "
                f"Trio bit {list(TRIO_SONATA_REQUIRED_BITS)} "
                f"(TrioVoiceIndependent=62); missing {miss}.\n"
            )
            return 2
    if phase == "Fantasia":
        miss = missing(FANTASIA_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "Fantasia closure requires the --required-rule-bit mask for the "
                f"Fantasia bit {list(FANTASIA_REQUIRED_BITS)} "
                f"(FantasiaSectionContrast=63); missing {miss}.\n"
            )
            return 2
    if phase == "KeyboardSuite":
        miss = missing(KEYBOARD_SUITE_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "KeyboardSuite closure requires --required-rule-bit masks for all three "
                f"reused suite bits {list(KEYBOARD_SUITE_REQUIRED_BITS)} "
                "(FigurationCommitted=52, FantasiaSectionContrast=63, "
                f"GroundBassReplayed=49); missing {miss}.\n"
            )
            return 2
    if phase == "PreludeAndFugue":
        miss = missing(PRELUDE_AND_FUGUE_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "PreludeAndFugue closure requires --required-rule-bit masks for both "
                f"reused prelude bits {list(PRELUDE_AND_FUGUE_REQUIRED_BITS)} "
                "(FigurationCommitted=52, PedalPreparation=54); "
                f"missing {miss}.\n"
            )
            return 2
    if phase == "GoldbergVariations":
        miss = missing(GOLDBERG_VARIATIONS_REQUIRED_BITS)
        if miss:
            sys.stderr.write(
                "GoldbergVariations closure requires --required-rule-bit masks for all three "
                f"dedicated Goldberg/climax bits {list(GOLDBERG_VARIATIONS_REQUIRED_BITS)} "
                "(GoldbergBassReplayed=65, GoldbergVariationRealized=66, "
                f"ClimaxPlaced=61); missing {miss}.\n"
            )
            return 2
    return None


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    """Attach the closure-harness CLI arguments to `parser`."""
    parser.add_argument("--phase", default="FugueExposition3v", help="FugueSubject2v/FugueSubject2vShort/FugueAnswer2v/FugueSubject3v/FugueExposition3v")
    parser.add_argument("--seeds", type=int, default=20)
    parser.add_argument("--threshold", type=float)
    parser.add_argument("--min-pass", type=int)
    parser.add_argument("--required-rule-bit", action="append", type=parse_required_rule_bit, default=[])
    parser.add_argument("--required-rule-min", type=int)
    parser.add_argument(
        "--all-bits-min",
        type=int,
        help="gate (4): minimum all-required-bits seeds (default: --seeds)",
    )
    parser.add_argument(
        "--jobs",
        type=int,
        default=1,
        help="number of seeds to generate/score in parallel (default 1 = sequential)",
    )
    parser.add_argument("--out", type=Path)
    parser.add_argument("--work-dir", type=Path)
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--bach-mcp-index", type=Path)
    parser.add_argument("--keep-work", action="store_true")


def register(subparsers) -> None:
    """Register the `closure` subcommand on a bach_tools subparser set."""
    parser = subparsers.add_parser(
        "closure",
        help="phase closure harness: generate seeds, score, check gates",
        description=(
            "Drives bach_cli --composer-phase <PhaseN> across seeds, scores the "
            "output with bach-mcp, and checks score thresholds, required rule "
            "bits, and byte-stable layout. The primary gate for composer changes."
        ),
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def main() -> int:
    parser = argparse.ArgumentParser()
    _add_arguments(parser)
    return run(parser.parse_args())


def run(args: argparse.Namespace) -> int:
    phase = normalize_phase(args.phase)
    if phase not in PHASE_DEFAULTS:
        sys.stderr.write(f"unknown phase: {args.phase}\n")
        return 2

    guard = _check_required_bits(phase, args.required_rule_bit)
    if guard is not None:
        return guard

    defaults = PHASE_DEFAULTS[phase]
    tag = defaults["tag"]
    threshold = args.threshold if args.threshold is not None else defaults["threshold"]
    min_pass = args.min_pass if args.min_pass is not None else defaults["min_pass"]
    required_rule_min = (
        args.required_rule_min if args.required_rule_min is not None else args.seeds
    )
    all_bits_min = args.all_bits_min if args.all_bits_min is not None else args.seeds
    report_path = args.out or (REPO_ROOT / "build" / f"closure_report_{tag}.json")
    work_dir = args.work_dir or (report_path.parent / f"closure_work_{tag}")
    index_js = args.bach_mcp_index or Path(os.environ.get("BACH_MCP_INDEX_JS", DEFAULT_INDEX_JS))

    if not args.cli.exists():
        sys.stderr.write(f"bach_cli missing: {args.cli}\n")
        return 2
    if not index_js.exists():
        sys.stderr.write(f"bach-mcp index.js missing: {index_js}\n")
        return 2

    try:
        prepare_work_dir(work_dir)
    except (OSError, ValueError) as exc:
        sys.stderr.write(f"{exc}\n")
        return 2
    report_path.parent.mkdir(parents=True, exist_ok=True)

    def run_seed(seed: int) -> dict[str, Any]:
        """Generate, score, and structurally check a single closure seed.

        Returns a self-contained per-seed row; all aggregate counters are
        derived later in a single pass over the sorted rows, so this is safe to
        run concurrently (distinct output_paths per seed, no shared state).

        @param seed Closure seed (0-based).
        @return The per-seed row dict.
        """
        midi, generated, provenance = output_paths(work_dir, tag, seed)
        fixture = fixture_for_seed(seed)
        # OrganToccata's offset = (seed // 4) % 4 cannot be recovered from the
        # harm_idx / subj_idx fields alone, so expose the raw seed for the
        # toccata structural predictor (harmless for every other phase).
        fixture["seed"] = seed
        proc = run_command(
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
            except RuntimeError as exc:
                # Surface scorer crash / non-JSON output as a run failure.
                row.update({"evaluator_ok": False, "evaluator_error": str(exc)})
            try:
                rule_hits = provenance_rule_counts(provenance, args.required_rule_bit)
                row["required_rule_hits"] = rule_hits
                # A seed where ALL required bits fire (AND across bits). Only
                # meaningful when at least one bit is required.
                all_bits_set = bool(rule_hits) and all(rule_hits.values())
                row["all_bits_set"] = all_bits_set
            except (OSError, json.JSONDecodeError) as exc:
                row["provenance_error"] = str(exc)
            structural = structural_check(generated, provenance, phase, fixture)
            row["structural"] = structural
        sys.stderr.write(
            f"[closure][{tag}][seed={seed}] "
            f"composer_ok={str(row['composer_ok']).lower()} "
            f"model_prob={row.get('model_prob', 0.0):.6f} "
            f"model_pass={str(row.get('model_pass', False)).lower()} "
            f"structural_ok={str(row.get('structural', {}).get('ok', False)).lower()}\n"
        )
        return row

    if args.jobs <= 1:
        rows = [run_seed(seed) for seed in range(args.seeds)]
    else:
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            rows = list(pool.map(run_seed, range(args.seeds)))
    # Deterministic order regardless of completion order; all aggregate
    # counters are computed in a single pass over the sorted rows below.
    rows.sort(key=lambda r: r["seed"])

    model_pass_count = 0
    composer_ok_count = 0
    structural_ok_count = 0
    rule_hit_counts = {name: 0 for name, _ in args.required_rule_bit}
    all_bits_seed_count = 0
    evaluator_error_count = 0
    for row in rows:
        if not row["composer_ok"]:
            continue
        composer_ok_count += 1
        if row.get("model_pass"):
            model_pass_count += 1
        if row.get("evaluator_ok") is False:
            evaluator_error_count += 1
        for name, hit in row.get("required_rule_hits", {}).items():
            if hit:
                rule_hit_counts[name] += 1
        if row.get("all_bits_set"):
            all_bits_seed_count += 1
        if row.get("structural", {}).get("ok"):
            structural_ok_count += 1

    rule_pass = {
        name: count >= required_rule_min for name, count in rule_hit_counts.items()
    }
    # Require all-bits-fire seeds in >= all_bits_min of the runs.
    # Only enforced when rule bits are actually being checked.
    all_bits_pass = (
        all_bits_seed_count >= all_bits_min if args.required_rule_bit else True
    )
    passed = compute_passed(
        seed_count=args.seeds,
        composer_ok_count=composer_ok_count,
        model_pass_count=model_pass_count,
        min_pass=min_pass,
        structural_ok_count=structural_ok_count,
        rule_pass=rule_pass,
        all_bits_pass=all_bits_pass,
        evaluator_error_count=evaluator_error_count,
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
        # structural_ok only validates the exposition entries (+ V0 stretto
        # leader for FugueComplete); counterline / development / NCT / rhythm carriers
        # are not modeled, so this scope is recorded explicitly.
        "structural_ok_scope": "exposition-entries-and-stretto-leader-only",
        # Number of seeds whose scorer crashed / returned non-JSON. Any
        # nonzero value forces passed=False (see compute_passed).
        "evaluator_error_count": evaluator_error_count,
        "required_rule_min": required_rule_min,
        "required_rule_counts": rule_hit_counts,
        "required_rule_pass": rule_pass,
        "all_bits_min": all_bits_min,
        "all_bits_seed_count": all_bits_seed_count,
        "all_bits_pass": all_bits_pass,
        "passed": passed,
        "seeds": rows,
    }
    report_path.write_text(json.dumps(report, indent=2, sort_keys=False) + "\n", encoding="utf-8")
    sys.stderr.write(
        f"[closure][{tag}][summary] composer_ok={composer_ok_count}/{args.seeds} "
        f"model_pass={model_pass_count}/{args.seeds} "
        f"structural_ok={structural_ok_count}/{composer_ok_count} "
        f"all_bits_seeds={all_bits_seed_count}/{args.seeds} "
        f"all_bits_pass={str(all_bits_pass).lower()} "
        f"evaluator_errors={evaluator_error_count}/{args.seeds} "
        f"report={report_path}\n"
    )
    # Compact one-line JSON summary to stdout for machine consumption.
    print(
        json.dumps(
            {
                "phase": phase,
                "passed": passed,
                "model_pass_count": model_pass_count,
                "structural_ok_count": structural_ok_count,
                "min_pass": min_pass,
                "threshold": threshold,
                "report": str(report_path),
            }
        )
    )

    if not args.keep_work:
        for path in work_dir.glob("*.mid"):
            path.unlink()
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
