"""Statistics generation: per-track/per-bar note counts, source breakdown, pitch range."""

from __future__ import annotations

import json
from collections import Counter, defaultdict
from typing import Any, Dict, List, Optional

from .model import TICKS_PER_BAR, Note, NoteSource, Score, pitch_to_name


def _bar_range(score: Score) -> range:
    """Return 1-based bar range covering the entire score."""
    return range(1, score.total_bars + 1)


def compute_stats(score: Score) -> Dict[str, Any]:
    """Compute comprehensive per-track/per-bar statistics.

    Returns a dict with:
      - metadata: seed, form, key, etc.
      - tracks: per-track summary (total notes, pitch range, source distribution)
      - bar_grid: per-bar, per-track note counts and source breakdown
      - bar_totals: per-bar aggregate across all tracks
      - source_summary: global source distribution
    """
    total_bars = score.total_bars
    track_names = [t.name for t in score.tracks]

    # -- Per-track summaries --
    track_summaries: List[Dict[str, Any]] = []
    for track in score.tracks:
        notes = track.sorted_notes
        if notes:
            pitches = [n.pitch for n in notes]
            track_summaries.append({
                "name": track.name,
                "channel": track.channel,
                "total_notes": len(notes),
                "pitch_min": min(pitches),
                "pitch_max": max(pitches),
                "pitch_min_name": pitch_to_name(min(pitches)),
                "pitch_max_name": pitch_to_name(max(pitches)),
                "avg_pitch": round(sum(pitches) / len(pitches), 1),
                "sources": _source_counts(notes),
            })
        else:
            track_summaries.append({
                "name": track.name,
                "channel": track.channel,
                "total_notes": 0,
            })

    # -- Per-bar, per-track grid --
    bar_grid: List[Dict[str, Any]] = []
    bar_totals: List[Dict[str, Any]] = []

    # Index notes by (track_name, bar)
    grid: Dict[str, Dict[int, List[Note]]] = defaultdict(lambda: defaultdict(list))
    for track in score.tracks:
        for note in track.notes:
            grid[track.name][note.bar].append(note)

    for bar in _bar_range(score):
        row: Dict[str, Any] = {"bar": bar}
        total_count = 0
        all_pitches: List[int] = []
        all_sources: List[Optional[NoteSource]] = []

        for tname in track_names:
            notes = grid[tname].get(bar, [])
            count = len(notes)
            total_count += count
            cell: Dict[str, Any] = {"n": count}
            if notes:
                pitches = [n.pitch for n in notes]
                all_pitches.extend(pitches)
                cell["lo"] = min(pitches)
                cell["hi"] = max(pitches)
                # Source breakdown (compact: only non-zero)
                src = _source_counts(notes)
                if src:
                    cell["src"] = src
                for n in notes:
                    if n.provenance:
                        all_sources.append(n.provenance.source)
            row[tname] = cell

        bar_total: Dict[str, Any] = {
            "bar": bar,
            "total": total_count,
        }
        if all_pitches:
            bar_total["lo"] = min(all_pitches)
            bar_total["hi"] = max(all_pitches)
        if all_sources:
            bar_total["src"] = _source_counter_to_dict(Counter(
                s.name.lower() for s in all_sources if s
            ))

        bar_grid.append(row)
        bar_totals.append(bar_total)

    # -- Global source summary --
    global_sources: Counter = Counter()
    for track in score.tracks:
        for note in track.notes:
            if note.provenance and note.provenance.source != NoteSource.UNKNOWN:
                global_sources[note.provenance.source.name.lower()] += 1

    return {
        "metadata": {
            "seed": score.seed,
            "form": score.form,
            "key": score.key,
            "num_voices": score.num_voices,
            "total_notes": score.total_notes,
            "total_bars": total_bars,
            "has_provenance": score.has_provenance,
        },
        "tracks": track_summaries,
        "bar_grid": bar_grid,
        "bar_totals": bar_totals,
        "source_summary": _source_counter_to_dict(global_sources),
    }


def _source_counts(notes: List[Note]) -> Dict[str, int]:
    """Count notes by provenance source, omitting UNKNOWN."""
    c: Counter = Counter()
    for n in notes:
        if n.provenance and n.provenance.source != NoteSource.UNKNOWN:
            c[n.provenance.source.name.lower()] += 1
    return _source_counter_to_dict(c)


def _source_counter_to_dict(c: Counter) -> Dict[str, int]:
    """Convert Counter to sorted dict (descending by count)."""
    return dict(sorted(c.items(), key=lambda x: -x[1]))


# ---------------------------------------------------------------------------
# Output formatters
# ---------------------------------------------------------------------------


def format_stats_text(data: Dict[str, Any]) -> str:
    """Format statistics as a human-readable text table."""
    lines: List[str] = []
    meta = data["metadata"]

    # Header
    parts = []
    if meta.get("seed") is not None:
        parts.append(f"seed={meta['seed']}")
    if meta.get("form"):
        parts.append(f"form={meta['form']}")
    if meta.get("key"):
        parts.append(f"key={meta['key']}")
    parts.append(f"{meta['num_voices']}v")
    parts.append(f"{meta['total_notes']} notes")
    parts.append(f"{meta['total_bars']} bars")
    lines.append(f"=== Stats: {', '.join(parts)} ===")
    lines.append("")

    # Track summaries
    lines.append("Track Summary:")
    for t in data["tracks"]:
        if t["total_notes"] == 0:
            lines.append(f"  {t['name']:<12} (empty)")
            continue
        lines.append(
            f"  {t['name']:<12} {t['total_notes']:>4} notes  "
            f"range {t['pitch_min_name']}-{t['pitch_max_name']} "
            f"({t['pitch_min']}-{t['pitch_max']})  avg={t['avg_pitch']}"
        )
        if t.get("sources"):
            src_parts = [f"{k}={v}" for k, v in list(t["sources"].items())[:5]]
            lines.append(f"{'':>16}{', '.join(src_parts)}")
    lines.append("")

    # Bar grid table
    track_names = [t["name"] for t in data["tracks"]]
    # Column widths
    bar_w = 4
    col_w = max(8, max((len(n) for n in track_names), default=8))

    header = f"{'Bar':>{bar_w}}"
    for tn in track_names:
        header += f"  {tn:>{col_w}}"
    header += f"  {'total':>{col_w}}"
    lines.append(header)
    lines.append("-" * len(header))

    for row, total in zip(data["bar_grid"], data["bar_totals"]):
        line = f"{row['bar']:>{bar_w}}"
        for tn in track_names:
            cell = row.get(tn, {})
            n = cell.get("n", 0)
            if n > 0 and "lo" in cell and "hi" in cell:
                span = cell["hi"] - cell["lo"]
                line += f"  {n:>{col_w - 4}}({span:>2})"
            else:
                line += f"  {n:>{col_w}}"
        line += f"  {total['total']:>{col_w}}"
        lines.append(line)
    lines.append("")

    # Source summary
    if data.get("source_summary"):
        lines.append("Source Distribution:")
        total = sum(data["source_summary"].values())
        for src, count in data["source_summary"].items():
            pct = count / total * 100 if total else 0
            bar = "#" * int(pct / 2)
            lines.append(f"  {src:<24} {count:>5} ({pct:>4.1f}%) {bar}")
        lines.append("")

    return "\n".join(lines)


def format_stats_json(data: Dict[str, Any]) -> str:
    """Format statistics as JSON."""
    return json.dumps(data, indent=2)
