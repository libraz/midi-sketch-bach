"""Tests for the machine-checkable Bach-technique catalog (task #173).

The load-bearing guard is test_no_drift_against_live_source: it runs the
catalog's cited evidence (RuleBit / Validator rule / VoiceIntent tokens)
against the actual C++ source, so a renamed/removed symbol that a catalog
entry still claims fails CI rather than silently inflating coverage.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import coverage as cov  # noqa: E402


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


class ShippedEvidenceTest(unittest.TestCase):
    """'implemented' must rest on evidence that default generation ships."""

    FORMS = (
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
    )
    SEEDS = range(1, 6)

    def setUp(self) -> None:
        self.catalog = cov.load_catalog()
        self.shipped = self.catalog["shipped_evidence"]

    def test_shipped_tokens_resolve_against_source(self) -> None:
        vocab = cov.known_tokens()
        for kind in ("rule_bits", "voice_intents"):
            unknown = set(self.shipped[kind]) - vocab[kind]
            self.assertFalse(unknown, f"shipped {kind} not in source: {sorted(unknown)}")

    def test_implemented_entries_cite_shipped_evidence(self) -> None:
        bits = set(self.shipped["rule_bits"])
        intents = set(self.shipped["voice_intents"])
        unsupported = [
            item["id"]
            for _, item in cov.iter_items(self.catalog)
            if item["status"] == "implemented"
            and not (set(item["evidence"]["rule_bits"]) & bits)
            and not (set(item["evidence"]["voice_intents"]) & intents)
        ]
        self.assertFalse(
            unsupported, f"implemented without shipped bit or voice intent: {unsupported}"
        )

    @unittest.skipUnless(os.environ.get("BACH_CLI"), "set BACH_CLI to a bach_cli binary")
    def test_shipped_evidence_matches_generation(self) -> None:
        names = {
            int(index): name
            for name, index in re.findall(
                r"(\w+)\s*=\s*(\d+)",
                cov.PROVENANCE_H.read_text(encoding="utf-8")
                .split("enum RuleBit", 1)[1]
                .split("};", 1)[0],
            )
        }
        bits: set[str] = set()
        intents: set[str] = set()
        with tempfile.TemporaryDirectory() as out_dir:
            for form in self.FORMS:
                for seed in self.SEEDS:
                    stem = os.path.join(out_dir, f"{form}_{seed}")
                    subprocess.run(
                        [os.environ["BACH_CLI"], "--form", form, "--seed", str(seed),
                         "--generated-json", "-o", stem + ".mid"],
                        check=True,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                    with open(stem + ".provenance.json", encoding="utf-8") as handle:
                        notes = json.load(handle)["notes"]
                    for note in notes:
                        mask = int(note["satisfied_rules"]) | (
                            int(note.get("satisfied_rules_high", "0")) << 64
                        )
                        bits.update(names[index] for index in names if mask >> index & 1)
                        intents.add(note["voice_intent"])
        self.assertEqual(sorted(bits), sorted(self.shipped["rule_bits"]))
        self.assertEqual(sorted(intents), sorted(self.shipped["voice_intents"]))


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

    def test_unknown_status_is_reported_without_crashing_coverage(self) -> None:
        catalog = {
            "domains": {
                "test": {
                    "items": [
                        {
                            "id": "bad",
                            "status": "future",
                            "evidence": {
                                "rule_bits": [],
                                "validator_rules": [],
                                "voice_intents": [],
                            },
                        }
                    ]
                }
            }
        }
        result = cov.compute_coverage(catalog)
        self.assertEqual(result["totals"]["count"], 1)
        problems = cov.validate_catalog(catalog, cov.known_tokens())
        self.assertTrue(problems["invalid_status"])


if __name__ == "__main__":
    unittest.main()
