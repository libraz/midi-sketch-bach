"""Output formatter for analysis results."""

from typing import Dict, Optional


def print_analysis(results: dict, verbose: bool = False) -> None:
    """Print analysis results in human-readable format.

    Args:
        results: Analysis results from CounterpointAnalyzer.full_analysis().
        verbose: If True, print additional detail.
    """
    print("=" * 60)
    print("Bach Counterpoint Analysis Report")
    print("=" * 60)
    print()

    print(f"Tracks analyzed: {results.get('num_tracks', 0)}")
    print(f"Total notes: {results.get('total_notes', 0)}")
    print()

    # Counterpoint violations
    print("--- Counterpoint Violations ---")
    parallel_fifths = results.get("parallel_fifths", 0)
    parallel_octaves = results.get("parallel_octaves", 0)
    voice_crossings = results.get("voice_crossings", 0)
    print(
        f"  Parallel fifths:  {parallel_fifths}  "
        f"{'PASS' if parallel_fifths == 0 else 'FAIL'}"
    )
    print(
        f"  Parallel octaves: {parallel_octaves}  "
        f"{'PASS' if parallel_octaves == 0 else 'FAIL'}"
    )
    print(
        f"  Voice crossings:  {voice_crossings}  "
        f"{'PASS' if voice_crossings == 0 else 'FAIL'}"
    )
    print()

    # Voice independence
    independence = results.get("voice_independence", {})
    print("--- Voice Independence ---")
    print(f"  Rhythm:    {independence.get('rhythm', 0):.3f}")
    print(f"  Contour:   {independence.get('contour', 0):.3f}")
    print(f"  Register:  {independence.get('register', 0):.3f}")
    print(f"  Composite: {independence.get('composite', 0):.3f}")
    print()

    # Overall
    all_pass = parallel_fifths == 0 and parallel_octaves == 0 and voice_crossings == 0
    composite = independence.get("composite", 0)
    print("--- Overall ---")
    print(f"  Counterpoint: {'PASS' if all_pass else 'FAIL'}")
    print(
        f"  Independence: "
        f"{'PASS' if composite >= 0.6 else 'NEEDS IMPROVEMENT'}"
    )
    print()


def print_fugue_structure(detection: dict) -> None:
    """Print fugue structure detection results.

    Args:
        detection: Detection results from FugueDetector.full_detection().
    """
    print("=" * 60)
    print("Fugue Structure Detection Report")
    print("=" * 60)
    print()

    # Exposition
    expo = detection.get("exposition")
    if expo:
        print(f"Exposition: tick {expo['start_tick']}-{expo['end_tick']}")
        print(f"  Voices entered: {expo['voices_entered']}")
        for entry in expo.get("entries", []):
            print(f"    {entry['voice']}: tick {entry['tick']} ({entry['source']})")
    else:
        print("Exposition: NOT DETECTED")
    print()

    # Subject entries
    entries = detection.get("subject_entries", [])
    print(f"Total subject entries: {len(entries)}")
    for entry in entries:
        print(
            f"  #{entry.get('entry_number', '?')}: {entry['voice']} "
            f"at tick {entry['tick']} ({entry['source']})"
        )
    print()

    # Episodes
    episodes = detection.get("episodes", [])
    print(f"Episodes detected: {len(episodes)}")
    for idx, episode in enumerate(episodes):
        bars = episode["duration_ticks"] / 1920
        print(
            f"  Episode {idx + 1}: tick {episode['start_tick']}-{episode['end_tick']} "
            f"({bars:.1f} bars)"
        )
    print()

    # Stretto
    strettos = detection.get("stretto", [])
    print(f"Stretto sections: {len(strettos)}")
    for stretto in strettos:
        print(
            f"  {stretto['voices'][0]} + {stretto['voices'][1]}: "
            f"overlap {stretto['overlap_ticks']} ticks"
        )
    print()


def format_as_json(results: dict) -> str:
    """Format results as JSON string.

    Args:
        results: Any analysis results dictionary.

    Returns:
        Pretty-printed JSON string.
    """
    import json

    return json.dumps(results, indent=2)
