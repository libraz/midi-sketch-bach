"""Report generation: text and JSON output."""

from __future__ import annotations

import json
from typing import Any, Dict, List, Optional

from .diagnosis import hotspot_ranking, source_file_for
from .model import Score
from .rules.base import Category, RuleResult, Severity, Violation


def _severity_prefix(severity: Severity) -> str:
    return {
        Severity.CRITICAL: "[CRITICAL]",
        Severity.ERROR: "[ERROR]   ",
        Severity.WARNING: "[WARNING] ",
        Severity.INFO: "[INFO]    ",
    }[severity]


def format_text(score: Score, results: List[RuleResult]) -> str:
    """Format validation results as human-readable text."""
    lines = []

    # Header
    meta_parts = []
    if score.seed is not None:
        meta_parts.append(f"seed={score.seed}")
    if score.form:
        meta_parts.append(f"form={score.form}")
    if score.key:
        meta_parts.append(f"key={score.key}")
    meta_parts.append(f"{score.num_voices} voices")
    lines.append(f"=== Validation: {', '.join(meta_parts)} ===")
    lines.append("")

    # Violations
    all_violations: List[Violation] = []
    for result in results:
        if result.violations:
            for v in result.violations:
                all_violations.append(v)
                line = f"{_severity_prefix(v.severity)} {v.category.value}/{v.rule_name}: {v.location}"
                if v.description:
                    line += f" {v.description}"
                lines.append(line)
                src = source_file_for(v.source)
                if src:
                    lines.append(f"           -> {v.source.name.lower() if v.source else ''} @ {src}")
        elif result.passed:
            lines.append(f"[PASS]     {result.category.value}/{result.rule_name}")

    lines.append("")

    # Category summary
    lines.append("Category Summary:")
    categories_seen: Dict[str, Dict[str, int]] = {}
    for result in results:
        cat = result.category.value
        if cat not in categories_seen:
            categories_seen[cat] = {"critical": 0, "error": 0, "warning": 0, "info": 0}
        categories_seen[cat]["critical"] += result.critical_count
        categories_seen[cat]["error"] += result.error_count
        categories_seen[cat]["warning"] += result.warning_count

    for cat, counts in sorted(categories_seen.items()):
        parts = []
        if counts["critical"]:
            parts.append(f"{counts['critical']} critical")
        if counts["error"]:
            parts.append(f"{counts['error']} error")
        if counts["warning"]:
            parts.append(f"{counts['warning']} warning")
        if parts:
            lines.append(f"  {cat:<16} FAIL ({', '.join(parts)})")
        else:
            lines.append(f"  {cat:<16} PASS")

    # Overall
    has_fail = any(c["critical"] + c["error"] > 0 for c in categories_seen.values())
    lines.append(f"  OVERALL: {'FAIL' if has_fail else 'PASS'}")
    lines.append("")

    return "\n".join(lines)


def format_json(score: Score, results: List[RuleResult]) -> str:
    """Format validation results as JSON."""
    data: Dict[str, Any] = {
        "metadata": {
            "seed": score.seed,
            "form": score.form,
            "key": score.key,
            "num_voices": score.num_voices,
            "total_notes": score.total_notes,
            "total_bars": score.total_bars,
            "source_file": score.source_file,
        },
        "results": [],
        "summary": {
            "overall_passed": True,
            "total_violations": 0,
            "critical": 0,
            "error": 0,
            "warning": 0,
        },
    }

    all_violations: List[Violation] = []
    for result in results:
        r: Dict[str, Any] = {
            "rule": result.rule_name,
            "category": result.category.value,
            "passed": result.passed,
            "violation_count": result.violation_count,
            "violations": [],
        }
        if result.info:
            r["info"] = result.info
        for v in result.violations:
            all_violations.append(v)
            vd: Dict[str, Any] = {
                "severity": v.severity.value,
                "bar": v.bar,
                "beat": v.beat,
                "tick": v.tick,
                "voice_a": v.voice_a,
                "description": v.description,
            }
            if v.voice_b:
                vd["voice_b"] = v.voice_b
            src = source_file_for(v.source)
            if src:
                vd["source_file"] = src
            r["violations"].append(vd)
        data["results"].append(r)

    total_c = sum(r.critical_count for r in results)
    total_e = sum(r.error_count for r in results)
    total_w = sum(r.warning_count for r in results)
    data["summary"]["total_violations"] = len(all_violations)
    data["summary"]["critical"] = total_c
    data["summary"]["error"] = total_e
    data["summary"]["warning"] = total_w
    data["summary"]["overall_passed"] = (total_c + total_e) == 0

    # Hotspots
    hotspots = hotspot_ranking(all_violations)
    if hotspots:
        data["hotspots"] = [
            {"source_file": path, "count": count, "percentage": f"{pct:.0%}"}
            for path, count, pct in hotspots
        ]

    return json.dumps(data, indent=2)


def format_batch_text(
    batch_results: List[Dict],
    form: str,
    seed_range: str,
    voices: Optional[int] = None,
) -> str:
    """Format batch validation results as text summary."""
    lines = []
    total = len(batch_results)
    passed = sum(1 for r in batch_results if r.get("overall_passed", False))

    lines.append(f"=== Batch: {form}, seeds {seed_range}, {voices or '?'} voices ===")
    lines.append(f"Pass rate: {passed}/{total} ({passed / total * 100:.0f}%)" if total else "No results")
    lines.append("")

    # Most common violations
    from collections import Counter
    violation_seeds: Dict[str, int] = Counter()
    violation_totals: Dict[str, int] = Counter()
    seed_violations: Dict[int, int] = {}

    for r in batch_results:
        seed = r.get("seed", 0)
        vcount = r.get("total_violations", 0)
        seed_violations[seed] = vcount
        for vname, count in r.get("violation_counts", {}).items():
            if count > 0:
                violation_seeds[vname] += 1
                violation_totals[vname] += count

    if violation_seeds:
        lines.append("Most common violations:")
        for vname, seed_count in violation_seeds.most_common(5):
            avg = violation_totals[vname] / seed_count if seed_count else 0
            lines.append(f"  {vname:<24} {seed_count} seeds  avg {avg:.1f}/seed")
        lines.append("")

    # Worst seeds
    worst = sorted(seed_violations.items(), key=lambda x: x[1], reverse=True)[:5]
    worst = [(s, v) for s, v in worst if v > 0]
    if worst:
        lines.append("Worst seeds: " + ", ".join(f"{s} ({v} violations)" for s, v in worst))
        lines.append("")

    return "\n".join(lines)


def _format_ranked_section(
    title: str,
    data: Dict,
    lines: List[str],
    max_items: int = 10,
) -> None:
    """Append a ranked breakdown section to lines."""
    lines.append(f"  {title}:")
    items = list(data.items())[:max_items]
    for key, info in items:
        lines.append(f"    {key:<32} {info['count']:>6}  ({info['pct']:>5.1f}%)")
    lines.append("")


def format_diagnostics_text(diagnostics_list: List[Dict]) -> str:
    """Format dissonance diagnostics as human-readable text.

    Args:
        diagnostics_list: List of dicts from compute_dissonance_diagnostics().
    """
    lines: List[str] = ["=== Dissonance Diagnostics ===", ""]

    for diag in diagnostics_list:
        rule = diag["rule"]
        lines.append(f"--- {rule} ---")
        lines.append(f"  Total violations: {diag['total_violations']}")
        lines.append(f"  Seeds analyzed: {diag['seeds_analyzed']}")
        lines.append(f"  Avg per seed: {diag['avg_per_seed']}")
        lines.append(f"  Unknown source rate: {diag['unknown_source_rate']:.2%}")
        lines.append("")

        _format_ranked_section("Source (note A)", diag["source_breakdown"], lines)
        _format_ranked_section("Source pair (A+B)", diag["source_pair_breakdown"], lines, 15)

        # Interval: show all (max 12)
        lines.append("  Interval distribution:")
        for iv_key, info in diag["interval_distribution"].items():
            lines.append(f"    {info['name']:<8} (iv={iv_key:>2})  {info['count']:>6}  ({info['pct']:>5.1f}%)")
        lines.append("")

        # Beat: show all
        lines.append("  Beat distribution:")
        for beat_key, info in diag["beat_distribution"].items():
            lines.append(f"    beat {beat_key:<4}              {info['count']:>6}  ({info['pct']:>5.1f}%)")
        lines.append("")

        _format_ranked_section("Voice pair", diag["voice_pair_breakdown"], lines)
        _format_ranked_section("Modified-by (bits)", diag["modified_by_bits"], lines)
        _format_ranked_section("Modified-by (primary)", diag["modified_by_primary"], lines)

    return "\n".join(lines)
