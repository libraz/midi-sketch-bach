"""Batch validation: generate + validate across multiple seeds via subprocess."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from collections import Counter
from pathlib import Path
from typing import Any, Dict, List, Optional

from .runner import load_score, overall_passed, validate
from .rules.base import RuleResult


def _run_bach_cli(
    seed: int,
    form: str,
    voices: Optional[int] = None,
    key: Optional[str] = None,
    cli_path: str = "./build/bin/bach_cli",
    output_dir: Optional[str] = None,
) -> Optional[Path]:
    """Generate a single output.json via bach_cli subprocess.

    Returns:
        Path to the output.json file, or None on failure.
    """
    if output_dir is None:
        output_dir = tempfile.mkdtemp(prefix="bach_batch_")
    out_path = Path(output_dir) / f"seed_{seed}.json"
    cmd = [
        cli_path,
        "--seed", str(seed),
        "--form", form,
        "--json",
        "-o", str(out_path),
    ]
    if voices is not None:
        cmd.extend(["--voices", str(voices)])
    if key is not None:
        cmd.extend(["--key", key])
    try:
        subprocess.run(cmd, check=True, capture_output=True, timeout=60)
        if out_path.exists():
            return out_path
    except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
        pass
    return None


def validate_seed(
    seed: int,
    form: str,
    voices: Optional[int] = None,
    key: Optional[str] = None,
    cli_path: str = "./build/bin/bach_cli",
    output_dir: Optional[str] = None,
    categories: Optional[set] = None,
) -> Dict[str, Any]:
    """Generate and validate a single seed.

    Returns:
        Dict with seed, overall_passed, total_violations, violation_counts.
    """
    result: Dict[str, Any] = {
        "seed": seed,
        "form": form,
        "overall_passed": False,
        "total_violations": 0,
        "violation_counts": {},
        "error": None,
    }
    out_path = _run_bach_cli(seed, form, voices, key, cli_path, output_dir)
    if out_path is None:
        result["error"] = "generation_failed"
        return result

    try:
        score = load_score(out_path)
        results = validate(score, categories=categories)
        result["overall_passed"] = overall_passed(results)
        all_violations = []
        counts: Dict[str, int] = {}
        for r in results:
            counts[r.rule_name] = r.violation_count
            all_violations.extend(r.violations)
        result["total_violations"] = len(all_violations)
        result["violation_counts"] = counts
    except Exception as exc:
        result["error"] = str(exc)
    return result


def run_batch(
    seeds: List[int],
    form: str,
    voices: Optional[int] = None,
    keys: Optional[List[str]] = None,
    cli_path: str = "./build/bin/bach_cli",
    categories: Optional[set] = None,
    on_progress: Optional[callable] = None,
) -> List[Dict[str, Any]]:
    """Run batch validation across multiple seeds.

    Args:
        seeds: List of seed values.
        form: Form type.
        voices: Number of voices.
        keys: Optional list of keys to cycle through.
        cli_path: Path to bach_cli binary.
        categories: Rule categories to check.
        on_progress: Optional callback(seed, idx, total) for progress.

    Returns:
        List of per-seed result dicts.
    """
    results = []
    key_list = keys or [None]
    total = len(seeds) * len(key_list)
    idx = 0
    with tempfile.TemporaryDirectory(prefix="bach_batch_") as tmpdir:
        for key in key_list:
            for seed in seeds:
                if on_progress:
                    on_progress(seed, idx, total)
                r = validate_seed(
                    seed=seed,
                    form=form,
                    voices=voices,
                    key=key,
                    cli_path=cli_path,
                    output_dir=tmpdir,
                    categories=categories,
                )
                if key:
                    r["key"] = key
                results.append(r)
                idx += 1
    return results


def parse_seed_range(seed_str: str) -> List[int]:
    """Parse seed range string like '1-50' or '1,5,10' into list of ints."""
    seeds = []
    for part in seed_str.split(","):
        part = part.strip()
        if "-" in part:
            start, end = part.split("-", 1)
            seeds.extend(range(int(start), int(end) + 1))
        else:
            seeds.append(int(part))
    return seeds


# ---------------------------------------------------------------------------
# Batch statistics
# ---------------------------------------------------------------------------


def compute_batch_statistics(batch_results: List[Dict[str, Any]]) -> Dict[str, Any]:
    """Compute aggregate statistics across batch results.

    Args:
        batch_results: List of per-seed result dicts from run_batch/validate_seed.

    Returns:
        Dict with per-rule stats, pass rate, worst seeds, and systemic violations.
    """
    import statistics

    total = len(batch_results)
    if total == 0:
        return {"per_rule": {}, "pass_rate": 0.0, "worst_seeds": [], "systemic_violations": []}

    passed = sum(1 for r in batch_results if r.get("overall_passed"))
    pass_rate = passed / total

    # Collect per-rule violation counts across all seeds.
    rule_counts: Dict[str, List[int]] = {}
    for result in batch_results:
        counts = result.get("violation_counts", {})
        for rule_name, count in counts.items():
            if rule_name not in rule_counts:
                rule_counts[rule_name] = []
            rule_counts[rule_name].append(count)

    # Compute per-rule statistics.
    per_rule: Dict[str, Dict[str, Any]] = {}
    for rule_name, counts in rule_counts.items():
        # Pad with zeros for seeds where the rule had 0 violations (not in counts).
        all_counts = counts + [0] * (total - len(counts))
        nonzero = [c for c in all_counts if c > 0]
        sorted_counts = sorted(all_counts)
        p95_idx = int(len(sorted_counts) * 0.95)
        per_rule[rule_name] = {
            "mean": statistics.mean(all_counts),
            "median": statistics.median(all_counts),
            "p95": sorted_counts[min(p95_idx, len(sorted_counts) - 1)],
            "max": max(all_counts) if all_counts else 0,
            "seeds_with_violations": len(nonzero),
            "systemic": len(nonzero) / total > 0.8,  # >80% of seeds = systemic
        }

    # Systemic violations: rules failing in >80% of seeds.
    systemic = [name for name, stats in per_rule.items() if stats["systemic"]]

    # Worst seeds: highest total violation count.
    seed_totals = [
        (r.get("seed", 0), r.get("total_violations", 0))
        for r in batch_results
    ]
    seed_totals.sort(key=lambda x: x[1], reverse=True)
    worst_seeds = [{"seed": s, "total_violations": v} for s, v in seed_totals[:5]]

    return {
        "per_rule": per_rule,
        "pass_rate": pass_rate,
        "total_seeds": total,
        "passed_seeds": passed,
        "worst_seeds": worst_seeds,
        "systemic_violations": systemic,
    }
