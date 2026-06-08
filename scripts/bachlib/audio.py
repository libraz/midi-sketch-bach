#!/usr/bin/env python3
"""Audio rendering and listening-packet assembly for the Composer tooling.

This module merges two concerns:

  - the audit WAV renderer (``render`` and helpers), which turns generated
    JSON note tracks into a simple organ-like WAV without external SoundFont
    dependencies, and
  - the listening-packet builder, which drives ``bach_cli`` across seeds,
    scores them, selects the top picks, and renders them to WAV.

Both ``render`` and ``listening`` are exposed as subcommands via
:func:`register`.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import sys
import wave
from array import array
from pathlib import Path
from typing import Any

from bachlib.common import (
    DEFAULT_CLI,
    DEFAULT_INDEX_JS,
    REPO_ROOT,
    heuristic_score,
    model_probability,
    run,
    score_generated,
)
from bachlib.phases import PHASE_TAGS, fixture_for_seed, normalize_phase

TICKS_PER_BEAT = 480
DEFAULT_SAMPLE_RATE = 44100


def midi_to_hz(pitch: int) -> float:
    """Convert a MIDI pitch number to frequency in Hz."""
    return 440.0 * (2.0 ** ((pitch - 69) / 12.0))


def render_note(
    pcm: array,
    *,
    pitch: int,
    velocity: int,
    start_sec: float,
    dur_sec: float,
    gain: float,
    sample_rate: int,
) -> None:
    """Additively mix one organ-like note into the PCM buffer."""
    if dur_sec <= 0.0:
        return

    start = max(0, int(start_sec * sample_rate))
    end = min(len(pcm), int((start_sec + dur_sec) * sample_rate))
    if end <= start:
        return

    freq = midi_to_hz(pitch)
    amp = gain * (max(1, min(127, velocity)) / 127.0)
    attack = max(8, int(0.006 * sample_rate))
    release = max(16, int(min(0.045, dur_sec * 0.18) * sample_rate))
    two_pi = 2.0 * math.pi

    for i in range(start, end):
        t = (i - start) / sample_rate
        local = i - start
        remaining = end - i
        env = 1.0
        if local < attack:
            env *= local / attack
        if remaining < release:
            env *= remaining / release

        # Principal + octave + gentle mutation stops.
        s = (
            0.78 * math.sin(two_pi * freq * t)
            + 0.24 * math.sin(two_pi * freq * 2.0 * t)
            + 0.13 * math.sin(two_pi * freq * 3.0 * t)
            + 0.08 * math.sin(two_pi * freq * 4.0 * t)
        )
        pcm[i] += amp * env * s


def track_gain(track: dict) -> float:
    """Pick a per-track gain based on the track name (pedal louder)."""
    name = str(track.get("name", "")).lower()
    if "pedal" in name:
        return 0.105
    if "manual i" in name:
        return 0.082
    return 0.074


def render(json_path: Path, wav_path: Path, sample_rate: int = DEFAULT_SAMPLE_RATE) -> None:
    """Render a generated JSON note file to a stereo WAV.

    @param json_path Path to a generated.v1 (or notes-array) JSON file.
    @param wav_path Output WAV path; parent directories are created.
    @param sample_rate Output sample rate in Hz.
    """
    data = json.loads(json_path.read_text(encoding="utf-8"))
    bpm = float(data.get("bpm", 72))
    seconds_per_tick = 60.0 / bpm / TICKS_PER_BEAT
    total_ticks = int(data.get("total_ticks", data.get("duration_ticks", 0)))

    notes: list[tuple[dict, dict]] = []
    tracks = data.get("tracks", [])
    if not tracks and isinstance(data.get("notes"), list):
        by_voice: dict[int, list[dict]] = {}
        for note in data["notes"]:
            by_voice.setdefault(int(note.get("voice", 0)), []).append(note)
        tracks = [
            {"name": f"Voice {voice}", "notes": voice_notes}
            for voice, voice_notes in sorted(by_voice.items())
        ]

    for track in tracks:
        for note in track.get("notes", []):
            notes.append((track, note))
            total_ticks = max(
                total_ticks,
                int(note.get("start_tick", 0)) + int(note.get("duration", 0)),
            )

    tail_sec = 1.8
    total_sec = total_ticks * seconds_per_tick + tail_sec
    pcm = array("f", [0.0]) * (int(total_sec * sample_rate) + 1)

    for track, note in notes:
        start_tick = int(note.get("start_tick", 0))
        duration = int(note.get("duration", 0))
        render_note(
            pcm,
            pitch=int(note.get("pitch", 60)),
            velocity=int(note.get("velocity", 80)),
            start_sec=start_tick * seconds_per_tick,
            dur_sec=duration * seconds_per_tick,
            gain=track_gain(track),
            sample_rate=sample_rate,
        )

    peak = max((abs(x) for x in pcm), default=1.0)
    scale = 0.93 / peak if peak > 0.93 else 1.0

    wav_path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(wav_path), "wb") as out:
        out.setnchannels(2)
        out.setsampwidth(2)
        out.setframerate(sample_rate)
        frames = bytearray()
        for sample in pcm:
            value = int(max(-1.0, min(1.0, sample * scale)) * 32767)
            packed = value.to_bytes(2, byteorder="little", signed=True)
            frames += packed
            frames += packed
        out.writeframes(frames)


def select_top(rows: list[dict[str, Any]], n: int) -> list[dict[str, Any]]:
    """Select up to ``n`` eligible rows, alternating eighth / quarter buckets."""
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


def _add_render_arguments(parser) -> None:
    """Register the ``render`` command arguments on ``parser``."""
    parser.add_argument("json_path", type=Path)
    parser.add_argument("wav_path", type=Path)
    parser.add_argument("--sample-rate", type=int, default=DEFAULT_SAMPLE_RATE)


def _add_listening_arguments(parser) -> None:
    """Register the ``listening`` command arguments on ``parser``."""
    parser.add_argument("--phase", default="FugueExposition3v", help="FugueSubject2v/FugueSubject2vShort/FugueAnswer2v/FugueSubject3v/FugueExposition3v")
    parser.add_argument("--top", type=int, default=5, help="number of seeds to render to WAV")
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "build" / "listening_packet")
    parser.add_argument("--cli", type=Path, default=DEFAULT_CLI)
    parser.add_argument("--bach-mcp-index", type=Path)
    parser.add_argument("--keep-json", action="store_true", help="keep all 20 generated JSON files")


def register(subparsers) -> None:
    """Register both the ``render`` and ``listening`` subcommands."""
    render_parser = subparsers.add_parser(
        "render",
        help="render generated JSON note tracks to a stereo WAV",
        description=render.__doc__,
    )
    _add_render_arguments(render_parser)
    render_parser.set_defaults(func=run_render)

    listening_parser = subparsers.add_parser(
        "listening",
        help="build a listening packet (drive bach_cli, score, render top seeds)",
        description=run_listening.__doc__,
    )
    _add_listening_arguments(listening_parser)
    listening_parser.set_defaults(func=run_listening)


def run_render(args) -> int:
    """Render a single JSON file to WAV from parsed CLI ``args``."""
    render(args.json_path, args.wav_path, args.sample_rate)
    return 0


def run_listening(args) -> int:
    """Build a listening packet from bach_cli Composer fixture output.

    @param args Namespace with phase / top / out / cli / bach_mcp_index /
        keep_json fields.
    @return 0 when at least one WAV was rendered, 1 when none were, 2 on
        missing prerequisites or an unknown phase.
    """
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
        render(src, wav)
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
