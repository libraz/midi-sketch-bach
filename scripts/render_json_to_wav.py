#!/usr/bin/env python3
"""Render generated JSON note tracks to a simple organ-like WAV file.

This is an audit renderer, not a production sample player. It avoids external
SoundFont dependencies so listening packets can be made in a bare workspace.
"""

from __future__ import annotations

import argparse
import json
import math
import wave
from array import array
from pathlib import Path


TICKS_PER_BEAT = 480
DEFAULT_SAMPLE_RATE = 44100


def midi_to_hz(pitch: int) -> float:
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
    name = str(track.get("name", "")).lower()
    if "pedal" in name:
        return 0.105
    if "manual i" in name:
        return 0.082
    return 0.074


def render(json_path: Path, wav_path: Path, sample_rate: int) -> None:
    data = json.loads(json_path.read_text(encoding="utf-8"))
    bpm = float(data.get("bpm", 72))
    seconds_per_tick = 60.0 / bpm / TICKS_PER_BEAT
    total_ticks = int(data.get("total_ticks", 0))

    notes: list[tuple[dict, dict]] = []
    for track in data.get("tracks", []):
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("json_path", type=Path)
    parser.add_argument("wav_path", type=Path)
    parser.add_argument("--sample-rate", type=int, default=DEFAULT_SAMPLE_RATE)
    args = parser.parse_args()
    render(args.json_path, args.wav_path, args.sample_rate)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
