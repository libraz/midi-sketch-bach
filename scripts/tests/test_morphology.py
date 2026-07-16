#!/usr/bin/env python3
"""Tests for the bach-mcp piano-roll morphology bridge."""

from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path

from bachlib.morphology import analyze_morphology, morphology_command


def report_json() -> str:
    return json.dumps(
        {
            "schema_version": "bach-morphology-report.v1",
            "candidate": {
                "schema_version": "bach-morphology-profile.v1",
                "grid": {"divisions_per_beat": 8, "width": 16},
            },
        }
    )


class MorphologyCommandTest(unittest.TestCase):
    def test_command_contains_optional_reference_and_bitmap_prefix(self) -> None:
        command = morphology_command(
            Path("/repo/bach-mcp/dist/index.js"),
            Path("/tmp/case.generated.json"),
            "BWV565",
            Path("/tmp/case-roll"),
            Path("/tmp/case.provenance.json"),
            16,
            8,
        )
        self.assertEqual(
            command,
            [
                "node",
                "/repo/bach-mcp/dist/index.js",
                "morphology",
                "/tmp/case.generated.json",
                "--reference",
                "BWV565",
                "--provenance",
                "/tmp/case.provenance.json",
                "--window-beats",
                "16",
                "--window-hop-beats",
                "8",
                "--bitmap-prefix",
                "/tmp/case-roll",
            ],
        )

    def test_command_omits_absent_options(self) -> None:
        self.assertEqual(
            morphology_command(Path("index.js"), Path("case.json")),
            ["node", "index.js", "morphology", "case.json"],
        )


class AnalyzeMorphologyTest(unittest.TestCase):
    def test_accepts_versioned_report(self) -> None:
        def runner(command: list[str]) -> subprocess.CompletedProcess[str]:
            return subprocess.CompletedProcess(command, 0, stdout=report_json(), stderr="")

        report = analyze_morphology(Path("index.js"), Path("case.json"), runner=runner)
        self.assertEqual(report["schema_version"], "bach-morphology-report.v1")

    def test_rejects_failed_mcp_process(self) -> None:
        def runner(command: list[str]) -> subprocess.CompletedProcess[str]:
            return subprocess.CompletedProcess(command, 1, stdout="", stderr="broken")

        with self.assertRaisesRegex(RuntimeError, "broken"):
            analyze_morphology(Path("index.js"), Path("case.json"), runner=runner)

    def test_rejects_non_json_output(self) -> None:
        def runner(command: list[str]) -> subprocess.CompletedProcess[str]:
            return subprocess.CompletedProcess(command, 0, stdout="not-json", stderr="")

        with self.assertRaisesRegex(RuntimeError, "non-JSON"):
            analyze_morphology(Path("index.js"), Path("case.json"), runner=runner)

    def test_rejects_wrong_report_schema(self) -> None:
        def runner(command: list[str]) -> subprocess.CompletedProcess[str]:
            return subprocess.CompletedProcess(
                command,
                0,
                stdout=json.dumps({"schema_version": "unknown"}),
                stderr="",
            )

        with self.assertRaisesRegex(RuntimeError, "unsupported"):
            analyze_morphology(Path("index.js"), Path("case.json"), runner=runner)

    def test_rejects_missing_candidate_profile(self) -> None:
        def runner(command: list[str]) -> subprocess.CompletedProcess[str]:
            return subprocess.CompletedProcess(
                command,
                0,
                stdout=json.dumps({"schema_version": "bach-morphology-report.v1"}),
                stderr="",
            )

        with self.assertRaisesRegex(RuntimeError, "candidate profile"):
            analyze_morphology(Path("index.js"), Path("case.json"), runner=runner)


if __name__ == "__main__":
    unittest.main()
