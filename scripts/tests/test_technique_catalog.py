"""Tests for the machine-checkable Bach-technique catalog (task #173).

The load-bearing guard is test_no_drift_against_live_source: it runs the
catalog's cited evidence (RuleBit / Validator rule / VoiceIntent tokens)
against the actual C++ source, so a renamed/removed symbol that a catalog
entry still claims fails CI rather than silently inflating coverage.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

import coverage_report as cov  # noqa: E402


class CatalogStructureTest(unittest.TestCase):
    def setUp(self) -> None:
        self.catalog = cov.load_catalog()

    def test_seven_domains(self) -> None:
        self.assertEqual(len(self.catalog["domains"]), 7)
        self.assertEqual(
            set(self.catalog["domains"]), set(self.catalog["expected_domain_counts"])
        )

    def test_domain_counts_match_declared(self) -> None:
        expected = self.catalog["expected_domain_counts"]
        for dk, domain in self.catalog["domains"].items():
            self.assertEqual(
                len(domain["items"]), expected[dk], f"{dk} item count != declared"
            )

    def test_total_is_105(self) -> None:
        # The 7 source tables list 21+15+25+21+10+6+7 = 105 rows; the plan's
        # historical "~104" headline was an approximation (domain 1 has 21).
        self.assertEqual(sum(self.catalog["expected_domain_counts"].values()), 105)
        total = sum(len(d["items"]) for d in self.catalog["domains"].values())
        self.assertEqual(total, 105)

    def test_ids_unique(self) -> None:
        ids = [item["id"] for _, item in cov.iter_items(self.catalog)]
        self.assertEqual(len(ids), len(set(ids)), "duplicate technique ids")

    def test_status_values_valid(self) -> None:
        for _, item in cov.iter_items(self.catalog):
            self.assertIn(item["status"], cov.VALID_STATUS, item["id"])

    def test_evidence_shape(self) -> None:
        # unimplemented => all evidence arrays empty; covered => at least one token.
        for _, item in cov.iter_items(self.catalog):
            ev = item["evidence"]
            self.assertEqual(set(ev), {"rule_bits", "validator_rules", "voice_intents"})
            n = sum(len(ev[k]) for k in ev)
            if item["status"] == "unimplemented":
                self.assertEqual(n, 0, f"{item['id']} unimplemented but cites evidence")
            else:
                self.assertGreater(n, 0, f"{item['id']} covered but cites no evidence")


class EvidenceVocabularyTest(unittest.TestCase):
    """The extractors must recover sane, non-empty vocabularies from source."""

    def test_rule_bits_anchor_symbols(self) -> None:
        bits = cov.extract_rule_bits()
        for anchor in ("ChordTone", "AffektCurveApplied", "ArpeggioFlowActive"):
            self.assertIn(anchor, bits)
        self.assertGreaterEqual(len(bits), 49)

    def test_voice_intents_anchor_symbols(self) -> None:
        intents = cov.extract_voice_intents()
        for anchor in ("SubjectCarrier", "ArpeggioFlow", "NctCarrier"):
            self.assertIn(anchor, intents)

    def test_validator_rules_anchor_symbols(self) -> None:
        rules = cov.extract_validator_rules()
        for anchor in ("parallel_fifth", "cross_relation", "arpeggio_no_parallel_perfect"):
            self.assertIn(anchor, rules)
        # The loose substring "resolution" is NOT a real rule_id; the proper
        # extractor must not surface it (regression guard for the #173 drift bug).
        self.assertNotIn("resolution", rules)


class DriftGuardTest(unittest.TestCase):
    def test_no_drift_against_live_source(self) -> None:
        catalog = cov.load_catalog()
        problems = cov.validate_catalog(catalog, cov.known_tokens())
        self.assertFalse(
            cov.has_problems(problems),
            f"catalog drifted from source: {problems}",
        )


class CoverageComputationTest(unittest.TestCase):
    def setUp(self) -> None:
        self.coverage = cov.compute_coverage(cov.load_catalog())

    def test_per_domain_tallies_reconcile(self) -> None:
        for dk, d in self.coverage["per_domain"].items():
            self.assertEqual(
                d["implemented"] + d["partial"] + d["unimplemented"],
                d["count"],
                f"{dk} tally != count",
            )

    def test_totals_reconcile(self) -> None:
        t = self.coverage["totals"]
        self.assertEqual(t["implemented"] + t["partial"] + t["unimplemented"], t["count"])
        self.assertEqual(t["count"], 105)
        self.assertGreater(t["weighted_coverage"], 0.0)
        self.assertLessEqual(t["weighted_coverage"], 1.0)


if __name__ == "__main__":
    unittest.main()
