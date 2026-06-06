#!/usr/bin/env python3
"""Texture diagnostics for generated.json note arrays.

This mirrors the C++ Validator info metrics so closure scripts can assert
texture gates without depending on composer internals.
"""

from __future__ import annotations

from dataclasses import dataclass, asdict
from typing import Any


@dataclass
class VoiceTextureMetrics:
    voice: int
    silence_ratio: float
    max_repeated_run: int
    min_pitch: int
    max_pitch: int


@dataclass
class TextureMetrics:
    max_active_voices: int
    avg_active_voices: float
    mono_ratio: float
    compass_violation_count: int
    register_overlap_ratio: float
    voices: list[VoiceTextureMetrics]

    def to_dict(self) -> dict[str, Any]:
        data = asdict(self)
        return data


def _active_at(note: dict[str, int], begin: int, end: int, voice: int) -> bool:
    return (
        note["voice"] == voice
        and note["start_tick"] < end
        and begin < note["start_tick"] + note["duration"]
    )


def compute_texture_metrics(notes: list[dict[str, int]]) -> TextureMetrics:
    """Compute piece-level texture metrics from a generated.v1 note array.

    The active-voice analysis decomposes the piece into half-open segments
    ``[begin, end)`` cut at every note onset and offset boundary (the sorted
    union of ``start_tick`` and ``start_tick + duration`` over all notes). The
    measured span is therefore ``[first onset, last offset]``; any silence
    before the first onset or after the last offset is not part of the piece
    span. A voice is active in a segment when one of its notes overlaps that
    segment (``start < end`` and ``begin < start + duration``); a note that
    only touches a boundary contributes no width. Each segment is weighted by
    its tick width.

    ``avg_active_voices`` is the tick-weighted mean active-voice count over
    those segments. ``mono_ratio`` is the tick-weighted fraction of that same
    span where EXACTLY ONE voice is sounding (segments whose active count is
    1). Both share the identical segmentation, so they are meter-independent:
    the result depends only on tick widths, not on ticks-per-bar, so a 3/4
    passacaglia at 1440 ticks/bar and a 4/4 piece at 1920 ticks/bar are scored
    on the same basis. With an empty note array both are ``0.0``.
    """
    if not notes:
        return TextureMetrics(0, 0.0, 0.0, 0, 0.0, [])

    voices = sorted({int(note["voice"]) for note in notes})
    boundaries = sorted(
        {
            tick
            for note in notes
            for tick in (
                int(note["start_tick"]),
                int(note["start_tick"]) + int(note["duration"]),
            )
        }
    )

    max_active = 0
    active_voice_ticks = 0
    mono_ticks = 0
    total_ticks = 0
    for begin, end in zip(boundaries, boundaries[1:]):
        if end <= begin:
            continue
        active = sum(
            1 for voice in voices if any(_active_at(note, begin, end, voice) for note in notes)
        )
        max_active = max(max_active, active)
        span = end - begin
        active_voice_ticks += active * span
        if active == 1:
            mono_ticks += span
        total_ticks += span

    voice_metrics: list[VoiceTextureMetrics] = []
    for voice in voices:
        voice_notes = sorted(
            (note for note in notes if int(note["voice"]) == voice),
            key=lambda note: (int(note["start_tick"]), int(note["pitch"])),
        )
        pitches = [int(note["pitch"]) for note in voice_notes]
        current_run = 0
        max_run = 1 if pitches else 0
        previous_pitch: int | None = None
        for pitch in pitches:
            current_run = current_run + 1 if pitch == previous_pitch else 1
            max_run = max(max_run, current_run)
            previous_pitch = pitch

        if voice_notes:
            first = min(int(note["start_tick"]) for note in voice_notes)
            last = max(int(note["start_tick"]) + int(note["duration"]) for note in voice_notes)
            sounding = sum(int(note["duration"]) for note in voice_notes)
            window = last - first
            silence = max(0.0, min(1.0, 1.0 - sounding / window)) if window > 0 else 0.0
            min_pitch = min(pitches)
            max_pitch = max(pitches)
        else:
            silence = 0.0
            min_pitch = 0
            max_pitch = 0

        voice_metrics.append(
            VoiceTextureMetrics(
                voice=voice,
                silence_ratio=silence,
                max_repeated_run=max_run,
                min_pitch=min_pitch,
                max_pitch=max_pitch,
            )
        )

    overlap_sum = 0.0
    pair_count = 0
    for index, left in enumerate(voice_metrics):
        for right in voice_metrics[index + 1 :]:
            overlap_lo = max(left.min_pitch, right.min_pitch)
            overlap_hi = min(left.max_pitch, right.max_pitch)
            union_lo = min(left.min_pitch, right.min_pitch)
            union_hi = max(left.max_pitch, right.max_pitch)
            overlap = max(0, overlap_hi - overlap_lo + 1)
            range_union = max(1, union_hi - union_lo + 1)
            overlap_sum += overlap / range_union
            pair_count += 1

    return TextureMetrics(
        max_active_voices=max_active,
        avg_active_voices=active_voice_ticks / total_ticks if total_ticks > 0 else 0.0,
        mono_ratio=mono_ticks / total_ticks if total_ticks > 0 else 0.0,
        compass_violation_count=sum(
            1 for note in notes if int(note["pitch"]) < 24 or int(note["pitch"]) > 84
        ),
        register_overlap_ratio=overlap_sum / pair_count if pair_count > 0 else 0.0,
        voices=voice_metrics,
    )
