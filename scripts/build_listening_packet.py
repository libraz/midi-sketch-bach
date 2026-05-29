#!/usr/bin/env python3
"""Build a listening packet from a TwentySeedHarness Phase run.

Drives the gtest harness with a writable TMPDIR so the per-seed
`bach_harness_<phase>_seed*.json` files persist, parses the per-seed
stderr summary for model_prob / heuristic, and renders the top-N seeds
(by model_prob) to WAV via `scripts/render_json_to_wav.py`. Writes a
`manifest.json` capturing every seed's metrics so the user can re-render
others on demand.
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

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_TEST_BIN = REPO_ROOT / "build" / "bin" / "bach_mcp_client_tests"
RENDER_SCRIPT = REPO_ROOT / "scripts" / "render_json_to_wav.py"

# [harness][p6][seed=0] composer_ok=true subject_ok=true answer_ok=true heur=0.82 model_prob=0.87 model_pass=true
SEED_RE = re.compile(
    r"\[harness\]\[(?P<phase>[^\]]+)\]\[seed=(?P<seed>\d+)\]"
    r"\s+composer_ok=(?P<composer_ok>true|false)"
    r"\s+subject_ok=(?P<subject_ok>true|false)"
    r"(?:\s+answer_ok=(?P<answer_ok>true|false))?"
    r"\s+heur=(?P<heur>[0-9.eE+-]+)"
    r"\s+model_prob=(?P<model_prob>[0-9.eE+-]+)"
    r"\s+model_pass=(?P<model_pass>true|false)"
)


def run_harness(test_bin: Path, phase_filter: str, tmp_dir: Path) -> str:
    tmp_dir.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env["TMPDIR"] = str(tmp_dir)
    cmd = [str(test_bin), f"--gtest_filter=TwentySeedHarness.{phase_filter}"]
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        sys.stderr.write(proc.stdout)
        sys.stderr.write(proc.stderr)
        raise SystemExit(f"harness exited {proc.returncode}")
    return proc.stderr


def parse_seeds(stderr_text: str) -> list[dict]:
    rows: list[dict] = []
    for line in stderr_text.splitlines():
        m = SEED_RE.search(line)
        if not m:
            continue
        rows.append(
            {
                "phase": m.group("phase"),
                "seed": int(m.group("seed")),
                "composer_ok": m.group("composer_ok") == "true",
                "subject_ok": m.group("subject_ok") == "true",
                "answer_ok": (
                    m.group("answer_ok") == "true" if m.group("answer_ok") else None
                ),
                "heuristic": float(m.group("heur")),
                "model_prob": float(m.group("model_prob")),
                "model_pass": m.group("model_pass") == "true",
            }
        )
    return rows


def fixture_for_seed(seed: int) -> dict:
    return {
        "subj_idx": (seed // 4) % 5,
        "harm_idx": seed % 4,
        "subdivision": "eighth" if (seed % 2) == 1 else "quarter",
    }


def select_top(rows: list[dict], n: int) -> list[dict]:
    # Balanced pick across subdivisions and subject patterns. Listening
    # diversity matters more than the absolute model_prob ceiling: a
    # purely top-N pick collapses to all-eighth, single-subject samples.
    eligible = [r for r in rows if r["composer_ok"] and r["model_pass"]]
    eligible.sort(key=lambda r: r["model_prob"], reverse=True)

    quarters = [r for r in eligible if fixture_for_seed(r["seed"])["subdivision"] == "quarter"]
    eighths = [r for r in eligible if fixture_for_seed(r["seed"])["subdivision"] == "eighth"]

    out: list[dict] = []
    seen_subj: set[int] = set()
    # Phase 6 / Phase 4 — alternate quarter/eighth so the packet covers
    # both subdivisions; within each subdivision prefer a new subj_idx
    # over a higher-prob duplicate.
    while len(out) < n and (quarters or eighths):
        for bucket in (eighths, quarters):
            if len(out) >= n:
                break
            picked = None
            for r in bucket:
                subj = fixture_for_seed(r["seed"])["subj_idx"]
                if subj not in seen_subj:
                    picked = r
                    break
            if picked is None and bucket:
                picked = bucket[0]
            if picked is not None:
                out.append(picked)
                seen_subj.add(fixture_for_seed(picked["seed"])["subj_idx"])
                bucket.remove(picked)
    return out


def render_wav(json_path: Path, wav_path: Path) -> None:
    cmd = [sys.executable, str(RENDER_SCRIPT), str(json_path), str(wav_path)]
    subprocess.run(cmd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", default="Phase6", help="gtest phase name (Phase3/Phase35/Phase4/Phase5/Phase6)")
    parser.add_argument("--top", type=int, default=5, help="number of seeds to render to WAV")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "backup" / "listening_p6")
    parser.add_argument("--test-bin", type=Path, default=DEFAULT_TEST_BIN)
    parser.add_argument("--keep-tmp", action="store_true", help="keep all 20 JSONs in out/")
    args = parser.parse_args()

    if not args.test_bin.exists():
        sys.stderr.write(f"test binary missing: {args.test_bin}\n")
        return 2
    if not RENDER_SCRIPT.exists():
        sys.stderr.write(f"render script missing: {RENDER_SCRIPT}\n")
        return 2

    out = args.out
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    json_dir = out / "json"
    wav_dir = out / "wav"
    json_dir.mkdir()
    wav_dir.mkdir()

    stderr = run_harness(args.test_bin, args.phase, json_dir)
    rows = parse_seeds(stderr)
    if not rows:
        sys.stderr.write("no per-seed summary lines parsed; harness output below:\n")
        sys.stderr.write(stderr)
        return 3

    phase_tag = rows[0]["phase"]
    selected = select_top(rows, args.top)

    rendered: list[dict] = []
    for entry in selected:
        seed = entry["seed"]
        src = json_dir / f"bach_harness_{phase_tag}_seed{seed}.json"
        if not src.exists():
            sys.stderr.write(f"missing generated JSON: {src}\n")
            continue
        wav = wav_dir / f"{phase_tag}_seed{seed:02d}.wav"
        render_wav(src, wav)
        rendered.append(
            {
                "seed": seed,
                "wav": wav.relative_to(out).as_posix(),
                "json": src.relative_to(out).as_posix() if args.keep_tmp else None,
                "model_prob": entry["model_prob"],
                "heuristic": entry["heuristic"],
                "fixture": fixture_for_seed(seed),
            }
        )

    if not args.keep_tmp:
        for path in json_dir.glob("bach_harness_*.json"):
            if not any(
                r["seed"] == int(re.search(r"seed(\d+)\.json$", path.name).group(1))
                for r in selected
            ):
                path.unlink()

    manifest = {
        "phase": phase_tag,
        "top_n": args.top,
        "all_seeds": [
            {
                **row,
                "fixture": fixture_for_seed(row["seed"]),
            }
            for row in rows
        ],
        "rendered": rendered,
    }
    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=False) + "\n", encoding="utf-8"
    )

    sys.stderr.write(f"wrote {len(rendered)} WAV(s) to {wav_dir}\n")
    sys.stderr.write(f"manifest: {out / 'manifest.json'}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
