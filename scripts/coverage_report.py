#!/usr/bin/env python3
"""Compute Bach-technique coverage from the machine-checkable catalog.

Replaces the prose-only '7 domains x ~104 items' estimate (deprecated
rebuild_plan_2026-05-28.md) with a computed, drift-guarded metric. Loads
scripts/technique_catalog.json and cross-checks every cited evidence token
against the live source of truth:

  rule_bits       -> RuleBit enum         (src/composer/provenance.h)
  validator_rules -> failure.rule_id lits  (src/composer/validator.cpp)
  voice_intents   -> VoiceIntent enum      (src/composer/voice_intent.h)

A catalog entry that claims a RuleBit / Validator rule / VoiceIntent which no
longer exists (renamed, removed, typo'd) is reported as an unresolved-evidence
drift and makes the run fail. Coverage is then computed per domain and overall.

Usage:
  python3 scripts/coverage_report.py            # human report, exit 1 on drift
  python3 scripts/coverage_report.py --json -o backup/coverage_report.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parent.parent
CATALOG_PATH = REPO_ROOT / "scripts" / "technique_catalog.json"
PROVENANCE_H = REPO_ROOT / "src" / "composer" / "provenance.h"
VOICE_INTENT_H = REPO_ROOT / "src" / "composer" / "voice_intent.h"
VALIDATOR_CPP = REPO_ROOT / "src" / "composer" / "validator.cpp"

VALID_STATUS = ("implemented", "partial", "unimplemented")
# Coverage weight per status; partial counts as half an implemented technique.
STATUS_WEIGHT = {"implemented": 1.0, "partial": 0.5, "unimplemented": 0.0}


def load_catalog(path: Path = CATALOG_PATH) -> dict[str, Any]:
    """Load and JSON-parse the technique catalog."""
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def extract_rule_bits(source: str | None = None) -> set[str]:
    """Names declared in the ``enum RuleBit`` block of provenance.h."""
    src = source if source is not None else PROVENANCE_H.read_text(encoding="utf-8")
    body = src.split("enum RuleBit", 1)[1].split("};", 1)[0]
    return {name for name, _ in re.findall(r"(\w+)\s*=\s*(\d+)", body)}


def extract_voice_intents(source: str | None = None) -> set[str]:
    """Names declared in the ``enum class VoiceIntent`` block of voice_intent.h."""
    src = source if source is not None else VOICE_INTENT_H.read_text(encoding="utf-8")
    body = src.split("enum class VoiceIntent", 1)[1].split("};", 1)[0]
    return {name for name, _ in re.findall(r"(\w+)\s*=\s*(\d+)", body)}


def extract_validator_rules(source: str | None = None) -> set[str]:
    """rule_id string literals assigned to ``failure.rule_id`` in validator.cpp.

    Handles both plain assignments and ternaries by pulling every string
    literal out of each ``rule_id = ... ;`` statement.
    """
    src = source if source is not None else VALIDATOR_CPP.read_text(encoding="utf-8")
    rules: set[str] = set()
    for stmt in re.findall(r"rule_id\s*=\s*(.*?);", src, re.S):
        rules.update(re.findall(r'"([a-z][a-z0-9_]+)"', stmt))
    return rules


def known_tokens() -> dict[str, set[str]]:
    """The three live evidence vocabularies, keyed by evidence kind."""
    return {
        "rule_bits": extract_rule_bits(),
        "validator_rules": extract_validator_rules(),
        "voice_intents": extract_voice_intents(),
    }


def iter_items(catalog: dict[str, Any]):
    """Yield (domain_key, item) over every catalog item."""
    for domain_key, domain in catalog["domains"].items():
        for item in domain["items"]:
            yield domain_key, item


def validate_catalog(
    catalog: dict[str, Any], vocab: dict[str, set[str]]
) -> dict[str, Any]:
    """Structural + drift checks. Returns a dict of problem lists (empty = OK)."""
    problems: dict[str, Any] = {
        "count_mismatches": [],
        "duplicate_ids": [],
        "bad_status": [],
        "empty_evidence_for_covered": [],
        "evidence_for_unimplemented": [],
        "unresolved_evidence": [],
    }
    expected = catalog["expected_domain_counts"]
    seen_ids: set[str] = set()

    for domain_key, domain in catalog["domains"].items():
        items = domain["items"]
        exp = expected.get(domain_key)
        if exp is None or len(items) != exp:
            problems["count_mismatches"].append(
                {"domain": domain_key, "expected": exp, "actual": len(items)}
            )

    for domain_key, item in iter_items(catalog):
        iid = item["id"]
        if iid in seen_ids:
            problems["duplicate_ids"].append(iid)
        seen_ids.add(iid)

        status = item.get("status")
        if status not in VALID_STATUS:
            problems["bad_status"].append({"id": iid, "status": status})

        evidence = item.get("evidence", {})
        total_tokens = sum(len(evidence.get(k, [])) for k in vocab)
        if status in ("implemented", "partial") and total_tokens == 0:
            problems["empty_evidence_for_covered"].append(iid)
        if status == "unimplemented" and total_tokens != 0:
            problems["evidence_for_unimplemented"].append(iid)

        for kind, valid in vocab.items():
            for token in evidence.get(kind, []):
                if token not in valid:
                    problems["unresolved_evidence"].append(
                        {"id": iid, "kind": kind, "token": token}
                    )

    return problems


def compute_coverage(catalog: dict[str, Any]) -> dict[str, Any]:
    """Per-domain and overall {implemented, partial, unimplemented} + weighted %."""
    per_domain: dict[str, Any] = {}
    totals = {"implemented": 0, "partial": 0, "unimplemented": 0, "count": 0}
    for domain_key, domain in catalog["domains"].items():
        tally = {"implemented": 0, "partial": 0, "unimplemented": 0}
        for item in domain["items"]:
            tally[item["status"]] += 1
        count = len(domain["items"])
        weighted = sum(STATUS_WEIGHT[s] * n for s, n in tally.items())
        per_domain[domain_key] = {
            **tally,
            "count": count,
            "weighted_coverage": round(weighted / count, 4) if count else 0.0,
        }
        for s in tally:
            totals[s] += tally[s]
        totals["count"] += count
    weighted_total = sum(STATUS_WEIGHT[s] * totals[s] for s in STATUS_WEIGHT)
    totals["weighted_coverage"] = (
        round(weighted_total / totals["count"], 4) if totals["count"] else 0.0
    )
    return {"per_domain": per_domain, "totals": totals}


def has_problems(problems: dict[str, Any]) -> bool:
    return any(problems[k] for k in problems)


def build_report() -> dict[str, Any]:
    catalog = load_catalog()
    vocab = known_tokens()
    problems = validate_catalog(catalog, vocab)
    coverage = compute_coverage(catalog)
    return {
        "schema_version": catalog.get("schema_version"),
        "problems": problems,
        "ok": not has_problems(problems),
        "coverage": coverage,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", action="store_true", help="emit JSON instead of text")
    parser.add_argument("--out", type=Path, help="write the JSON report to this path")
    args = parser.parse_args()

    report = build_report()

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2, sort_keys=False) + "\n", encoding="utf-8")

    if args.json:
        sys.stdout.write(json.dumps(report, indent=2, sort_keys=False) + "\n")
    else:
        cov = report["coverage"]
        t = cov["totals"]
        sys.stdout.write("Bach-technique coverage (machine-computed)\n")
        sys.stdout.write(f"{'domain':<22} impl part unimpl  /count  weighted\n")
        for dk, d in cov["per_domain"].items():
            sys.stdout.write(
                f"{dk:<22} {d['implemented']:>4} {d['partial']:>4} "
                f"{d['unimplemented']:>6}  /{d['count']:<5} {d['weighted_coverage']:.1%}\n"
            )
        sys.stdout.write(
            f"{'TOTAL':<22} {t['implemented']:>4} {t['partial']:>4} "
            f"{t['unimplemented']:>6}  /{t['count']:<5} {t['weighted_coverage']:.1%}\n"
        )
        if not report["ok"]:
            sys.stdout.write("\nPROBLEMS:\n")
            for kind, entries in report["problems"].items():
                if entries:
                    sys.stdout.write(f"  {kind}: {entries}\n")

    return 0 if report["ok"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
