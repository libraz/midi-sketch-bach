#!/usr/bin/env python3
"""Run Phase B-3 melodic shadow-weight sweeps.

This drives the composer closure harness with BACH_MELODIC_SELECTION=shadow,
scores generated.v1 output through bach-mcp, and writes a ranked markdown
summary. It is intentionally resumable by configuration index; the full 512
configuration grid is expensive.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import itertools
import json
import os
import subprocess
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from closure_common import normalize_phase
from run_phase_closure import PHASE_DEFAULTS, model_probability

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CLI = REPO_ROOT / "build" / "bin" / "bach_cli"
DEFAULT_BACH_MCP = REPO_ROOT.parent / "bach-mcp" / "dist" / "index.js"
DEFAULT_OUT = REPO_ROOT / "backup" / "weight_sweep_b3.md"
DEFAULT_RESULTS_JSONL = REPO_ROOT / "backup" / "weight_sweep_b3_results.jsonl"
DEFAULT_PHASES = [
    "Phase7",
    "Phase8",
    "Phase9",
    "Phase10",
    "Phase11",
    "Phase12",
    "Phase14",
    "Phase15",
    "Phase16",
    "Phase17",
    "Phase18",
    "Phase19",
    "Phase20",
    "Phase21",
    "Phase22",
    "Phase23",
    "Phase24",
    "Phase25",
]
DEFAULT_FORMS = [
    "fugue",
    "prelude_and_fugue",
    "trio_sonata",
    "chorale_prelude",
    "toccata_and_fugue",
    "passacaglia",
    "fantasia_and_fugue",
    "cello_prelude",
    "chaconne",
    "goldberg_variations",
]
DEFAULT_LEVELS = [0.0, 0.5, 1.0, 2.0]


@dataclass(frozen=True)
class SweepConfig:
    wp: float
    wr: float
    wsd: float
    wm: float
    step_bonus: int

    @property
    def l1(self) -> float:
        return abs(self.wp) + abs(self.wr) + abs(self.wsd) + abs(self.wm) + self.step_bonus

    def env(self) -> dict[str, str]:
        return {
            "BACH_MELODIC_SELECTION": "shadow",
            "BACH_MELODIC_WP": str(self.wp),
            "BACH_MELODIC_WR": str(self.wr),
            "BACH_MELODIC_WSD": str(self.wsd),
            "BACH_MELODIC_WM": str(self.wm),
            "BACH_MELODIC_STEP_BONUS": str(self.step_bonus),
        }

    def label(self) -> str:
        return (
            f"wp={self.wp:g}, wr={self.wr:g}, wsd={self.wsd:g}, "
            f"wm={self.wm:g}, step={self.step_bonus}"
        )

    def to_dict(self) -> dict[str, float | int]:
        return {
            "wp": self.wp,
            "wr": self.wr,
            "wsd": self.wsd,
            "wm": self.wm,
            "step_bonus": self.step_bonus,
        }

    @staticmethod
    def from_dict(data: dict[str, Any]) -> "SweepConfig":
        return SweepConfig(
            wp=float(data["wp"]),
            wr=float(data["wr"]),
            wsd=float(data["wsd"]),
            wm=float(data["wm"]),
            step_bonus=int(data["step_bonus"]),
        )


@dataclass
class SweepResult:
    index: int
    config: SweepConfig
    generated: int
    validation_failed: int
    model_pass: int
    required_pass: int
    mean_probability: float
    min_probability: float | None

    @property
    def feasible(self) -> bool:
        return self.validation_failed == 0 and self.model_pass >= self.required_pass

    def to_record(self, target: str, names: list[str], seeds: list[int],
                  min_pass_mode: str) -> dict[str, Any]:
        return {
            "index": self.index,
            "config": self.config.to_dict(),
            "target": target,
            "names": names,
            "seeds": seeds,
            "min_pass_mode": min_pass_mode,
            "generated": self.generated,
            "validation_failed": self.validation_failed,
            "model_pass": self.model_pass,
            "required_pass": self.required_pass,
            "mean_probability": self.mean_probability,
            "min_probability": self.min_probability,
        }

    @staticmethod
    def from_record(data: dict[str, Any]) -> "SweepResult":
        return SweepResult(
            index=int(data["index"]),
            config=SweepConfig.from_dict(data["config"]),
            generated=int(data["generated"]),
            validation_failed=int(data["validation_failed"]),
            model_pass=int(data["model_pass"]),
            required_pass=int(data["required_pass"]),
            mean_probability=float(data["mean_probability"]),
            min_probability=(
                None if data.get("min_probability") is None else float(data["min_probability"])
            ),
        )


def parse_levels(value: str) -> list[float]:
    levels = [float(part.strip()) for part in value.split(",") if part.strip()]
    if not levels:
        raise ValueError("at least one level is required")
    return levels


def iter_configs(levels: list[float]) -> Iterable[SweepConfig]:
    for wp, wr, wsd, wm, step in itertools.product(levels, levels, levels, levels, [0, 1]):
        yield SweepConfig(wp=wp, wr=wr, wsd=wsd, wm=wm, step_bonus=step)


def score_file(bach_mcp: Path, json_path: Path) -> dict[str, Any]:
    completed = subprocess.run(
        ["node", str(bach_mcp), "score", str(json_path)],
        cwd=REPO_ROOT,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return json.loads(completed.stdout)


def run_phase_case(cli: Path, bach_mcp: Path, phase: str, seed: int, config: SweepConfig,
                   work_dir: Path) -> dict[str, Any] | None:
    midi_path = work_dir / f"{phase}_{seed}.mid"
    env = os.environ.copy()
    env.update(config.env())
    completed = subprocess.run(
        [
            str(cli),
            "--composer-phase",
            phase,
            "--seed",
            str(seed),
            "--json",
            "-o",
            str(midi_path),
        ],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        return None
    return score_file(bach_mcp, midi_path.with_suffix(".json"))


def run_form_case(cli: Path, bach_mcp: Path, form: str, seed: int, config: SweepConfig,
                  work_dir: Path) -> dict[str, Any] | None:
    midi_path = work_dir / f"{form}_{seed}.mid"
    env = os.environ.copy()
    env.update(config.env())
    completed = subprocess.run(
        [
            str(cli),
            "--form",
            form,
            "--seed",
            str(seed),
            "--generated-json",
            "-o",
            str(midi_path),
        ],
        cwd=REPO_ROOT,
        env=env,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if completed.returncode != 0:
        return None
    return score_file(bach_mcp, midi_path.with_suffix(".generated.json"))


def model_pass_for_score(target: str, name: str, score: dict[str, Any]) -> bool:
    if target == "forms":
        model_score = score.get("model_score")
        return isinstance(model_score, dict) and model_score.get("pass") is True
    threshold = float(PHASE_DEFAULTS[name]["threshold"])
    return model_probability(score) >= threshold


def required_pass_count(target: str, names: list[str], seeds: list[int], mode: str) -> int:
    if target == "forms" or mode == "all":
        return len(names) * len(seeds)
    required = 0
    for phase in names:
        if len(seeds) == 20:
            required += int(PHASE_DEFAULTS[phase]["min_pass"])
        else:
            threshold = int(PHASE_DEFAULTS[phase]["min_pass"]) / 20.0
            required += int(round(threshold * len(seeds)))
    return required


def evaluate_config(index: int, config: SweepConfig, cli: Path, bach_mcp: Path, target: str,
                    names: list[str], seeds: list[int], min_pass_mode: str) -> SweepResult:
    probabilities: list[float] = []
    model_pass = 0
    generated = 0
    validation_failed = 0
    with tempfile.TemporaryDirectory(prefix=f"bach-b3-sweep-{index}-") as tmp:
        work_dir = Path(tmp)
        for name in names:
            for seed in seeds:
                if target == "forms":
                    score = run_form_case(cli, bach_mcp, name, seed, config, work_dir)
                else:
                    score = run_phase_case(cli, bach_mcp, name, seed, config, work_dir)
                if score is None:
                    validation_failed += 1
                    continue
                generated += 1
                probability = model_probability(score)
                probabilities.append(probability)
                if model_pass_for_score(target, name, score):
                    model_pass += 1
    required = required_pass_count(target, names, seeds, min_pass_mode)
    mean = sum(probabilities) / len(probabilities) if probabilities else 0.0
    return SweepResult(
        index=index,
        config=config,
        generated=generated,
        validation_failed=validation_failed,
        model_pass=model_pass,
        required_pass=required,
        mean_probability=mean,
        min_probability=min(probabilities) if probabilities else None,
    )


def rank_results(results: list[SweepResult]) -> list[SweepResult]:
    return sorted(
        results,
        key=lambda r: (
            0 if r.feasible else 1,
            -r.mean_probability,
            r.config.l1,
            r.index,
        ),
    )


def load_results_jsonl(path: Path, target: str, names: list[str], seeds: list[int],
                       min_pass_mode: str) -> list[SweepResult]:
    if not path.is_file():
        return []
    results: list[SweepResult] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        record = json.loads(line)
        if record.get("target") != target:
            continue
        if record.get("names") != names:
            continue
        if record.get("seeds") != seeds:
            continue
        if record.get("min_pass_mode") != min_pass_mode:
            continue
        results.append(SweepResult.from_record(record))
    return results


def append_result_jsonl(path: Path, result: SweepResult, target: str, names: list[str],
                        seeds: list[int], min_pass_mode: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(
            json.dumps(result.to_record(target, names, seeds, min_pass_mode), sort_keys=True)
            + "\n"
        )


def render_report(results: list[SweepResult], target: str, names: list[str], seeds: list[int],
                  min_pass_mode: str, levels: list[float]) -> str:
    ranked = rank_results(results)
    feasible = [r for r in ranked if r.feasible]
    lines = [
        "# Phase B-3 Melodic Weight Sweep",
        "",
        f"- levels: {', '.join(f'{v:g}' for v in levels)}",
        f"- target: {target}",
        f"- {'forms' if target == 'forms' else 'phases'}: {', '.join(names)}",
        f"- seeds: {seeds[0]}..{seeds[-1]}" if seeds else "- seeds: none",
        f"- min_pass_mode: {min_pass_mode}",
        f"- configs_evaluated: {len(results)}",
        f"- feasible_configs: {len(feasible)}",
        "",
        "## Top Configurations",
        "",
        "| rank | index | feasible | mean_prob | min_prob | model_pass | validation_failed | config |",
        "|---:|---:|:---:|---:|---:|---:|---:|---|",
    ]
    for rank, result in enumerate(ranked[:5], start=1):
        min_prob = "n/a" if result.min_probability is None else f"{result.min_probability:.6f}"
        lines.append(
            f"| {rank} | {result.index} | {'yes' if result.feasible else 'no'} | "
            f"{result.mean_probability:.6f} | {min_prob} | "
            f"{result.model_pass}/{result.required_pass} | {result.validation_failed} | "
            f"{result.config.label()} |"
        )
    lines.extend(
        [
            "",
            "Selection rule: feasible configurations sort ahead of infeasible ones,",
            "then by highest mean model probability, then by lowest L1 weight norm.",
            "",
        ]
    )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--bach-mcp", type=Path, default=DEFAULT_BACH_MCP)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--results-jsonl", type=Path, default=DEFAULT_RESULTS_JSONL)
    parser.add_argument("--levels", default=",".join(str(v) for v in DEFAULT_LEVELS))
    parser.add_argument("--seeds", type=int, default=20)
    parser.add_argument("--phase", action="append", dest="phases")
    parser.add_argument("--form", action="append", dest="forms")
    parser.add_argument("--target", choices=["forms", "phases"], default="forms")
    parser.add_argument("--start-index", type=int, default=0)
    parser.add_argument("--limit-configs", type=int)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--min-pass-mode", choices=["phase-default", "all"], default="phase-default")
    args = parser.parse_args()

    levels = parse_levels(args.levels)
    if args.target == "forms":
        names = args.forms or DEFAULT_FORMS
        min_pass_mode = "all"
    else:
        names = [normalize_phase(p) for p in (args.phases or DEFAULT_PHASES)]
        min_pass_mode = args.min_pass_mode
    seeds = list(range(args.seeds))
    configs = list(iter_configs(levels))
    selected = list(enumerate(configs))[args.start_index:]
    if args.limit_configs is not None:
        selected = selected[: args.limit_configs]

    existing_results = load_results_jsonl(args.results_jsonl, args.target, names, seeds, min_pass_mode)
    existing_indices = {result.index for result in existing_results}
    work_items = [
        (index, config)
        for index, config in selected
        if not (args.resume and index in existing_indices)
    ]

    run_results: list[SweepResult] = []
    if args.jobs <= 1:
        evaluated = [
            evaluate_config(index, config, args.cli, args.bach_mcp, args.target, names, seeds,
                            min_pass_mode)
            for index, config in work_items
        ]
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as executor:
            futures = [
                executor.submit(evaluate_config, index, config, args.cli, args.bach_mcp,
                                args.target, names, seeds, min_pass_mode)
                for index, config in work_items
            ]
            evaluated = [future.result() for future in futures]

    for result in sorted(evaluated, key=lambda r: r.index):
        append_result_jsonl(args.results_jsonl, result, args.target, names, seeds, min_pass_mode)
        run_results.append(result)

    results = load_results_jsonl(args.results_jsonl, args.target, names, seeds, min_pass_mode)
    if not results:
        results = run_results
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(render_report(results, args.target, names, seeds, min_pass_mode, levels),
                        encoding="utf-8")
    print(f"wrote {args.out}")
    print(f"recorded {len(run_results)} new configs in {args.results_jsonl}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
