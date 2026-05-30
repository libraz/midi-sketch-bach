#!/usr/bin/env python3
"""Build a listening packet from bach_cli Composer phase output."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

from closure_common import fixture_for_seed, normalize_phase

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_CLI = REPO_ROOT / "build" / "bin" / "bach_cli"
DEFAULT_INDEX_JS = REPO_ROOT.parent / "bach-mcp" / "dist" / "index.js"
RENDER_SCRIPT = REPO_ROOT / "scripts" / "render_json_to_wav.py"

PHASE_TAGS = {
    "Phase3": "p3",
    "Phase35": "p3.5",
    "Phase4": "p4",
    "Phase5": "p5",
    "Phase6": "p6",
    # P14 milestone: first listening packet for the all-technique fugue.
    "Phase14": "p14",
}


def model_probability(score: dict[str, Any]) -> float:
    model = score.get("model_score")
    if isinstance(model, dict):
        value = model.get("probability", 0.0)
        if isinstance(value, (int, float)):
            return float(value)
    return 0.0


def heuristic_score(score: dict[str, Any]) -> float:
    value = score.get("score", 0.0)
    return float(value) if isinstance(value, (int, float)) else 0.0


def run(cmd: list[str], *, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False)


def score_generated(index_js: Path, generated_json: Path) -> dict[str, Any]:
    proc = run(["node", str(index_js), "score", str(generated_json)])
    if proc.returncode != 0:
        raise RuntimeError(proc.stdout + proc.stderr)
    return json.loads(proc.stdout)


def select_top(rows: list[dict[str, Any]], n: int) -> list[dict[str, Any]]:
    eligible = [r for r in rows if r["composer_ok"] and r.get("evaluator_ok", False)]
    eligible.sort(key=lambda r: r["model_prob"], reverse=True)

    quarters = [r for r in eligible if fixture_for_seed(r["seed"])["subdivision"] == "quarter"]
    eighths = [r for r in eligible if fixture_for_seed(r["seed"])["subdivision"] == "eighth"]

    out: list[dict[str, Any]] = []
    seen_subj: set[int] = set()
    while len(out) < n and (quarters or eighths):
        for bucket in (eighths, quarters):
            if len(out) >= n:
                break
            picked = None
            for row in bucket:
                subj = fixture_for_seed(row["seed"])["subj_idx"]
                if subj not in seen_subj:
                    picked = row
                    break
            if picked is None and bucket:
                picked = bucket[0]
            if picked is not None:
                out.append(picked)
                seen_subj.add(fixture_for_seed(picked["seed"])["subj_idx"])
                bucket.remove(picked)
    return out


def render_wav(json_path: Path, wav_path: Path) -> None:
    subprocess.run([sys.executable, str(RENDER_SCRIPT), str(json_path), str(wav_path)], check=True)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--phase", default="Phase6", help="Phase3/Phase35/Phase4/Phase5/Phase6")
    parser.add_argument("--top", type=int, default=5, help="number of seeds to render to WAV")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "backup" / "listening_p6")
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--bach-mcp-index", type=Path)
    parser.add_argument("--keep-json", action="store_true", help="keep all 20 generated JSON files")
    args = parser.parse_args()

    phase = normalize_phase(args.phase)
    if phase not in PHASE_TAGS:
        sys.stderr.write(f"unknown phase: {args.phase}\n")
        return 2
    tag = PHASE_TAGS[phase]
    index_js = args.bach_mcp_index or Path(os.environ.get("BACH_MCP_INDEX_JS", DEFAULT_INDEX_JS))

    if not args.cli.exists():
        sys.stderr.write(f"bach_cli missing: {args.cli}\n")
        return 2
    if not index_js.exists():
        sys.stderr.write(f"bach-mcp index.js missing: {index_js}\n")
        return 2
    if not RENDER_SCRIPT.exists():
        sys.stderr.write(f"render script missing: {RENDER_SCRIPT}\n")
        return 2

    out = args.out
    if out.exists():
        shutil.rmtree(out)
    json_dir = out / "json"
    wav_dir = out / "wav"
    json_dir.mkdir(parents=True)
    wav_dir.mkdir()

    rows: list[dict[str, Any]] = []
    for seed in range(20):
        midi = json_dir / f"bach_harness_{tag}_seed{seed}.mid"
        generated = midi.with_suffix(".json")
        provenance = midi.with_suffix(".provenance.json")
        proc = run(
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
            "composer_ok": proc.returncode == 0,
            "fixture": fixture_for_seed(seed),
            "generated_json": generated,
            "provenance_json": provenance,
        }
        if proc.returncode == 0:
            try:
                score = score_generated(index_js, generated)
                row.update(
                    {
                        "evaluator_ok": True,
                        "heuristic": heuristic_score(score),
                        "model_prob": model_probability(score),
                    }
                )
            except (RuntimeError, json.JSONDecodeError) as exc:
                row.update({"evaluator_ok": False, "evaluator_error": str(exc)})
        rows.append(row)

    selected = select_top(rows, args.top)
    rendered: list[dict[str, Any]] = []
    for entry in selected:
        seed = entry["seed"]
        src = Path(entry["generated_json"])
        wav = wav_dir / f"{tag}_seed{seed:02d}.wav"
        render_wav(src, wav)
        rendered.append(
            {
                "seed": seed,
                "wav": wav.relative_to(out).as_posix(),
                "json": src.relative_to(out).as_posix(),
                "provenance": Path(entry["provenance_json"]).relative_to(out).as_posix(),
                "model_prob": entry["model_prob"],
                "heuristic": entry["heuristic"],
                "fixture": entry["fixture"],
            }
        )

    if not args.keep_json:
        keep = {Path(entry["generated_json"]) for entry in selected}
        keep.update(Path(entry["provenance_json"]) for entry in selected)
        for path in json_dir.glob("bach_harness_*"):
            if path.suffix == ".mid" or path not in keep:
                path.unlink()

    manifest = {
        "phase": tag,
        "top_n": args.top,
        "all_seeds": [
            {
                **{k: v for k, v in row.items() if k not in {"generated_json", "provenance_json"}},
                "generated_json": Path(row["generated_json"]).relative_to(out).as_posix(),
                "provenance_json": Path(row["provenance_json"]).relative_to(out).as_posix(),
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
    return 0 if rendered else 1


if __name__ == "__main__":
    raise SystemExit(main())
