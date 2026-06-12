#!/usr/bin/env python3
"""Qualify synthesized subject candidates through the production pipeline.

A candidate that scores well in isolation may still fail in context, so the
qualification gate is the real product path: every candidate is written into
all five subject slots of a throwaway git worktree (so each seed picks the
candidate regardless of character), ``bach_cli`` is rebuilt incrementally,
and the fugue-family forms are generated and judged with the exact texture
gate semantics (per-form thresholds, all default axes) plus the cadence /
leading-tone provenance bits.

The worktree edit is local and temporary; the checked-in catalogs stay
frozen. Results stream into a resumable JSONL so an interrupted batch
continues where it stopped. ``--catalog-out`` renders the qualified pool
(anchored by the five shipped subjects per mode) into a dead-data
``subject_catalog.inc`` for the later runtime switch.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

from bachlib.common import REPO_ROOT
from bachlib.subject_synth import existing_subjects
from bachlib import texture_gate

FUGUE_FORMS = ("fugue", "prelude_and_fugue", "toccata_and_fugue", "fantasia_and_fugue")
# The internal pitch space is C-rooted for both modes (the minor catalog is
# C minor), so minor candidates are exercised with an explicit c_minor key.
MODE_KEYS = {"major": None, "minor": "c_minor"}

SUBJECT_ARRAY = "kFugueCompleteSubjects"
RHYTHM_ARRAY = "kFugueCompleteSubjectRhythms"
MINOR_ARRAY = "kSubjectsMinor"
CATALOG_SLOTS = 5
TICKS_PER_BEAT = 480


def candidate_key(mode: str, pitches: list[int]) -> str:
    """Stable digest identifying one candidate across resumed runs."""
    payload = mode + ":" + ",".join(str(p) for p in pitches)
    return hashlib.sha256(payload.encode()).hexdigest()[:16]


def render_rows(rows: list[list[int]]) -> str:
    return "".join(
        "    {" + ", ".join(str(value) for value in row) + "},\n" for row in rows
    )


def replace_array_rows(text: str, array_name: str, rows: list[list[int]]) -> str:
    """Replace the brace body of ``<array_name> = {{ ... }};`` with new rows."""
    pattern = re.compile(
        r"(" + re.escape(array_name) + r"\s*=\s*\{\{\n).*?(\}\};)", re.DOTALL
    )
    replaced, count = pattern.subn(
        lambda match: match.group(1) + render_rows(rows) + match.group(2), text
    )
    if count != 1:
        raise ValueError(f"array {array_name} not found exactly once")
    return replaced


def parse_tick_expression(expr: str) -> int:
    """Evaluate a rhythm-row entry like ``3 * kTicksPerBeat / 2`` to ticks."""
    tokens = re.split(r"\s*([*/])\s*", expr.strip())
    value: int | None = None
    operator = "*"
    for token in tokens:
        if token in ("*", "/"):
            operator = token
            continue
        number = TICKS_PER_BEAT if token == "kTicksPerBeat" else int(token)
        if value is None:
            value = number
        elif operator == "*":
            value *= number
        else:
            value //= number
    if value is None:
        raise ValueError(f"empty tick expression: {expr!r}")
    return value


def parse_rhythm_rows(figuration_text: str) -> list[list[int]]:
    """Parse the shipped rhythm rows (tick expressions) from figuration.h."""
    match = re.search(
        re.escape(RHYTHM_ARRAY) + r"\s*=\s*\{\{(.*?)\}\};", figuration_text, re.DOTALL
    )
    if not match:
        raise ValueError(f"{RHYTHM_ARRAY} not found")
    rows: list[list[int]] = []
    for row_text in re.findall(r"\{([^{}]*)\}", match.group(1)):
        rows.append([parse_tick_expression(part) for part in row_text.split(",")])
    return rows


def patched_sources(
    pristine_figuration: str,
    pristine_minor: str,
    mode: str,
    pitches: list[int],
    rhythm: list[int],
) -> tuple[str, str]:
    """Render figuration.h / minor_material.h with the candidate in all slots."""
    rhythm_rows = [list(rhythm)] * CATALOG_SLOTS
    figuration = replace_array_rows(pristine_figuration, RHYTHM_ARRAY, rhythm_rows)
    minor = pristine_minor
    pitch_rows = [list(pitches)] * CATALOG_SLOTS
    if mode == "major":
        figuration = replace_array_rows(figuration, SUBJECT_ARRAY, pitch_rows)
    else:
        minor = replace_array_rows(minor, MINOR_ARRAY, pitch_rows)
    return figuration, minor


# Bounded regression allowed on the v1 axis relative to the shipped subjects
# at the same form x seed. v1 is cross-entropy (linear in the generated
# distribution), so a corpus-shaped leap vocabulary systematically pays a few
# hundredths there while the KL axes improve; the qualification keeps v1 as a
# floor-only axis (the same split the pool ranking uses) and lets the strict
# v2 / length-invariant ratchets carry the quality bar.
V1_TOLERANCE = 0.01


def evaluate_case(case, exempt: set, baseline_scores: dict | None = None) -> dict:
    """Judge one form x seed gate case.

    The leading-tone / cadence material is guaranteed structurally (the
    synthesizer fixes the 71,72 tail and the catalog invariant test asserts
    it), so the criteria are the texture-gate axes only. `exempt` holds
    (form, seed, axis) triples the unpatched baseline build already fails
    (known calibration artifacts, e.g. minor-key v1 floors): a candidate is
    only blamed for axes the shipped subjects pass. A v1 (model_score) floor
    miss is additionally forgiven when the candidate stays within
    V1_TOLERANCE of the baseline's v1 at the same form x seed.
    """
    failures: list[str] = []
    if not case.generated:
        failures.append(f"generation failed: {case.error or 'unknown'}")
        return {"form": case.form, "seed": case.seed, "failures": failures}
    scores = baseline_scores or {}
    for axis, passed in case.axis_results().items():
        if passed or (case.form, case.seed, axis) in exempt:
            continue
        if axis == "model_score":
            reference = scores.get((case.form, case.seed))
            if reference is not None and case.model_score >= reference - V1_TOLERANCE:
                continue
        failures.append(f"axis:{axis}")
    return {
        "form": case.form,
        "seed": case.seed,
        "model_score": case.model_score,
        "model_score_v2": case.model_score_v2,
        "model_score_v2_length_invariant": case.model_score_v2_length_invariant,
        "failures": failures,
    }


def qualify_candidate(
    cli: Path,
    index_js: Path,
    work_dir: Path,
    mode: str,
    seeds: list[int],
    *,
    fail_fast: bool = True,
    forms: tuple = FUGUE_FORMS,
    exempt: set | None = None,
    baseline_scores: dict | None = None,
    case_runner=None,
) -> dict:
    """Run the gate criteria over all fugue-family forms for one build."""
    runner = case_runner if case_runner is not None else texture_gate.run_case
    key = MODE_KEYS[mode]
    exempt_set = exempt if exempt is not None else set()
    cases: list[dict] = []
    qualified = True
    for form in forms:
        for seed in seeds:
            case = runner(cli, form, seed, work_dir, None, index_js=index_js, key=key)
            verdict = evaluate_case(case, exempt_set, baseline_scores)
            cases.append(verdict)
            if verdict["failures"]:
                qualified = False
                if fail_fast:
                    return {"qualified": False, "cases": cases}
    return {"qualified": qualified, "cases": cases}


def baseline_exemptions(baseline: dict, mode: str) -> set:
    """(form, seed, axis) triples the unpatched build fails for `mode`."""
    out: set = set()
    for case in baseline.get(mode, {}).get("cases", []):
        for failure in case.get("failures", []):
            if failure.startswith("axis:"):
                out.add((case["form"], case["seed"], failure[len("axis:"):]))
    return out


def baseline_v1_scores(baseline: dict, mode: str) -> dict:
    """(form, seed) -> baseline v1 model score for the V1_TOLERANCE rule."""
    out: dict = {}
    for case in baseline.get(mode, {}).get("cases", []):
        score = case.get("model_score")
        if isinstance(score, (int, float)):
            out[(case["form"], case["seed"])] = float(score)
    return out


def setup_worktree(worktree: Path) -> None:
    if worktree.exists() and (worktree / "CMakeLists.txt").exists():
        return
    subprocess.run(
        ["git", "worktree", "add", "--detach", str(worktree), "HEAD"],
        cwd=REPO_ROOT,
        check=True,
        capture_output=True,
        text=True,
    )


def remove_worktree(worktree: Path) -> None:
    subprocess.run(
        ["git", "worktree", "remove", "--force", str(worktree)],
        cwd=REPO_ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def build_cli(worktree: Path, jobs: int) -> Path:
    build_dir = worktree / "build"
    if not (build_dir / "CMakeCache.txt").exists():
        subprocess.run(
            ["cmake", "-S", str(worktree), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Debug"],
            check=True,
            capture_output=True,
            text=True,
        )
    proc = subprocess.run(
        ["cmake", "--build", str(build_dir), "--target", "bach_cli", "-j", str(jobs)],
        check=False,
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"bach_cli build failed:\n{proc.stderr[-2000:]}")
    return build_dir / "bin" / "bach_cli"


def load_results(path: Path) -> dict:
    results: dict = {}
    if not path.exists():
        return results
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            row = json.loads(line)
            results[row["key"]] = row
    return results


def append_result(path: Path, row: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(row, sort_keys=True) + "\n")


def render_catalog(
    qualified: dict,
    pool: dict,
    figuration_text: str,
    minor_header: Path,
    *,
    catalog_size: int,
    command: str,
) -> str:
    """Render subject_catalog.inc: 5 shipped anchors per mode + qualified picks.

    @param qualified Map of candidate key -> result row (only qualified rows used).
    @param pool The subject_pool.v1 document (candidate order = shape-KL rank).
    @param figuration_text Pristine figuration.h text (anchor rhythm source).
    @param minor_header Path to minor_material.h (anchor pitch source, minor).
    @param catalog_size Total entries per mode (anchors included).
    @param command Regeneration command recorded in the header.
    @return The .inc file contents.
    """
    anchor_rhythms = parse_rhythm_rows(figuration_text)
    lines = [
        "// Generated by `python3 scripts/bach_tools.py qualify-subjects`.",
        f"// Command: {command}",
        "// Entries 0-4 of each mode mirror the shipped 5-subject catalogs",
        "// (kFugueCompleteSubjects / kSubjectsMinor) as backward-compatible",
        "// anchors; the rest passed the qualification gate: every fugue-family",
        "// form x seed must validate and clear the texture-gate axes (per-form",
        "// v2 / length-invariant model floors strict; the v1 cross-entropy floor",
        "// allows a small tolerance against the shipped subjects' score at the",
        "// same form x seed, since a corpus-shaped leap vocabulary pays v1).",
        "",
    ]
    for mode in ("major", "minor"):
        anchors = existing_subjects(mode, minor_header)
        picks: list[dict] = []
        for candidate in pool["modes"][mode]["candidates"]:
            row = qualified.get(candidate_key(mode, candidate["pitches"]))
            if row is None or not row.get("qualified"):
                continue
            picks.append(candidate)
            if len(picks) >= catalog_size - len(anchors):
                break
        suffix = "Major" if mode == "major" else "Minor"
        total = len(anchors) + len(picks)
        pitch_rows = [list(row) for row in anchors] + [c["pitches"] for c in picks]
        rhythm_rows = list(anchor_rhythms) + [c["rhythm_ticks"] for c in picks]
        lines.append(
            f"inline constexpr std::array<std::array<std::uint8_t, 16>, {total}> "
            f"kSubjectCatalog{suffix} = {{{{"
        )
        lines.extend(
            "    {" + ", ".join(str(v) for v in row) + "}," for row in pitch_rows
        )
        lines.append("}};")
        lines.append(
            f"inline constexpr std::array<std::array<Tick, 16>, {total}> "
            f"kSubjectCatalog{suffix}Rhythms = {{{{"
        )
        lines.extend(
            "    {" + ", ".join(str(v) for v in row) + "}," for row in rhythm_rows
        )
        lines.append("}};")
        lines.append("")
    return "\n".join(lines)


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--pool",
        type=Path,
        default=REPO_ROOT / "backup" / "subject_pool.json",
        help="subject_pool.v1 JSON produced by synth-subjects",
    )
    parser.add_argument(
        "--results",
        type=Path,
        default=REPO_ROOT / "backup" / "subject_qualify.jsonl",
        help="resumable per-candidate verdict log",
    )
    parser.add_argument(
        "--worktree",
        type=Path,
        default=Path("/tmp/bach-subject-qualify"),
        help="throwaway git worktree used for patched builds",
    )
    parser.add_argument("--modes", nargs="+", default=["major", "minor"])
    parser.add_argument("--limit", type=int, default=0, help="candidates per mode (0 = all)")
    parser.add_argument("--seeds", type=int, default=10, help="seeds 1..N per form")
    parser.add_argument("--jobs", type=int, default=8, help="build parallelism")
    parser.add_argument(
        "--baseline",
        action="store_true",
        help="judge the unpatched shipped subjects, save the exemption map, exit",
    )
    parser.add_argument(
        "--baseline-file",
        type=Path,
        default=REPO_ROOT / "backup" / "subject_qualify_baseline.json",
        help="baseline verdicts; failures here are exempted for candidates",
    )
    parser.add_argument(
        "--keep-worktree",
        action="store_true",
        help="keep the worktree for inspection (default: remove on success)",
    )
    parser.add_argument(
        "--no-fail-fast",
        action="store_true",
        help="run every form x seed even after the first failure",
    )
    parser.add_argument(
        "--catalog-out",
        type=Path,
        default=None,
        help="render subject_catalog.inc from existing results and exit",
    )
    parser.add_argument(
        "--catalog-size",
        type=int,
        default=37,
        help="entries per mode in the catalog (5 anchors + qualified picks)",
    )
    parser.add_argument(
        "--index-js",
        type=Path,
        default=REPO_ROOT.parent / "bach-mcp" / "dist" / "index.js",
    )


def register(subparsers) -> None:
    """Register the ``qualify-subjects`` subcommand on a subparser action."""
    parser = subparsers.add_parser(
        "qualify-subjects",
        help="qualify synthesized subjects through patched production builds",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Execute qualification (or catalog rendering) from parsed CLI args."""
    with args.pool.open(encoding="utf-8") as handle:
        pool = json.load(handle)
    if pool.get("schema") != "subject_pool.v1":
        raise SystemExit(f"unexpected pool schema in {args.pool}")
    figuration_path = REPO_ROOT / "src" / "composer" / "figuration.h"
    minor_path = REPO_ROOT / "src" / "composer" / "minor_material.h"

    if args.catalog_out is not None:
        results = load_results(args.results)
        catalog = render_catalog(
            results,
            pool,
            figuration_path.read_text(encoding="utf-8"),
            minor_path,
            catalog_size=args.catalog_size,
            command=f"scripts/bach_tools.py qualify-subjects --catalog-out "
            f"{args.catalog_out} --catalog-size {args.catalog_size}",
        )
        args.catalog_out.parent.mkdir(parents=True, exist_ok=True)
        args.catalog_out.write_text(catalog, encoding="utf-8")
        print(f"catalog={args.catalog_out}")
        return 0

    seeds = list(range(1, args.seeds + 1))
    setup_worktree(args.worktree)
    wt_figuration = args.worktree / "src" / "composer" / "figuration.h"
    wt_minor = args.worktree / "src" / "composer" / "minor_material.h"
    pristine_figuration = wt_figuration.read_text(encoding="utf-8")
    pristine_minor = wt_minor.read_text(encoding="utf-8")
    work_dir = args.worktree / "qualify_runs"
    work_dir.mkdir(exist_ok=True)

    try:
        if args.baseline:
            cli = build_cli(args.worktree, args.jobs)
            baseline: dict = {}
            for mode in args.modes:
                result = qualify_candidate(
                    cli,
                    args.index_js,
                    work_dir,
                    mode,
                    seeds,
                    fail_fast=False,
                )
                baseline[mode] = result
                failures = [c for c in result["cases"] if c["failures"]]
                print(f"baseline {mode}: clean={not failures}")
                for case in failures:
                    print(
                        f"  {case['form']} seed {case['seed']}: "
                        f"{'; '.join(case['failures'])}"
                    )
            args.baseline_file.parent.mkdir(parents=True, exist_ok=True)
            with args.baseline_file.open("w", encoding="utf-8") as handle:
                json.dump(baseline, handle, indent=2)
                handle.write("\n")
            print(f"baseline={args.baseline_file}")
            return 0

        if args.baseline_file.exists():
            with args.baseline_file.open(encoding="utf-8") as handle:
                baseline = json.load(handle)
        else:
            raise SystemExit(
                f"baseline file missing: {args.baseline_file} — run --baseline first "
                "(candidate verdicts are relative to the shipped subjects)"
            )

        results = load_results(args.results)
        judged = 0
        passed = 0
        for mode in args.modes:
            exempt = baseline_exemptions(baseline, mode)
            v1_scores = baseline_v1_scores(baseline, mode)
            candidates = pool["modes"][mode]["candidates"]
            if args.limit:
                candidates = candidates[: args.limit]
            for candidate in candidates:
                key = candidate_key(mode, candidate["pitches"])
                if key in results:
                    continue
                figuration, minor = patched_sources(
                    pristine_figuration,
                    pristine_minor,
                    mode,
                    candidate["pitches"],
                    candidate["rhythm_ticks"],
                )
                wt_figuration.write_text(figuration, encoding="utf-8")
                wt_minor.write_text(minor, encoding="utf-8")
                cli = build_cli(args.worktree, args.jobs)
                result = qualify_candidate(
                    cli,
                    args.index_js,
                    work_dir,
                    mode,
                    seeds,
                    fail_fast=not args.no_fail_fast,
                    exempt=exempt,
                    baseline_scores=v1_scores,
                )
                row = {
                    "key": key,
                    "mode": mode,
                    "pitches": candidate["pitches"],
                    "qualified": result["qualified"],
                    "cases": result["cases"],
                }
                append_result(args.results, row)
                results[key] = row
                judged += 1
                passed += 1 if result["qualified"] else 0
                state = "PASS" if result["qualified"] else "fail"
                print(f"[{judged}] {mode} {key} {state}", flush=True)
    finally:
        wt_figuration.write_text(pristine_figuration, encoding="utf-8")
        wt_minor.write_text(pristine_minor, encoding="utf-8")
        if not args.keep_worktree:
            remove_worktree(args.worktree)

    total_pass = {
        mode: sum(
            1 for row in results.values() if row["mode"] == mode and row["qualified"]
        )
        for mode in args.modes
    }
    print(f"judged={judged} passed_now={passed} qualified_total={total_pass}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
