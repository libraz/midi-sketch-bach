#!/usr/bin/env python3
"""Generate data/reference/key_signatures.json from BWV key signature mappings.

Maps each work_id (file stem from data/reference/*.json) to its key signature
with tonic, mode, and confidence level.

Usage:
    python3 scripts/generate_key_signatures.py
"""

import json
import sys
from pathlib import Path

# Project root (script lives in scripts/)
PROJECT_ROOT = Path(__file__).resolve().parent.parent
REFERENCE_DIR = PROJECT_ROOT / "data" / "reference"
OUTPUT_FILE = REFERENCE_DIR / "key_signatures.json"


def make_entry(tonic: str, mode: str, confidence: str = "verified") -> dict:
    """Create a key signature entry."""
    return {"tonic": tonic, "mode": mode, "confidence": confidence}


def build_wtc_keys() -> dict[int, tuple[str, str]]:
    """Build the chromatic key cycle used by both WTC books.

    Returns a dict mapping offset (0-23) to (tonic, mode).
    The cycle: C maj, C min, C# maj, C# min, D maj, D min, ...
    """
    # WTC key cycle: chromatic ascending, major then minor for each pitch
    tonics = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
    # Special cases in the WTC naming:
    # BWV 862 = Ab major (not G# major)
    # BWV 863 = G# minor (not Ab minor)
    # These are already handled by the tonic list above since we use
    # enharmonic conventions matching the actual BWV catalog.

    cycle = {}
    for idx, tonic in enumerate(tonics):
        cycle[idx * 2] = (tonic, "major")
        cycle[idx * 2 + 1] = (tonic, "minor")

    # Override specific enharmonic spellings per BWV catalog convention
    # BWV 863 (offset 17) is G# minor, not Ab minor
    cycle[17] = ("G#", "minor")

    return cycle


def generate_wtc1_mappings() -> dict[str, dict]:
    """WTC Book 1: BWV 846-869, prelude + fugue each."""
    mappings = {}
    cycle = build_wtc_keys()
    for idx in range(24):
        bwv = 846 + idx
        tonic, mode = cycle[idx]
        entry = make_entry(tonic, mode)
        mappings[f"BWV{bwv}_prelude"] = entry
        mappings[f"BWV{bwv}_fugue"] = entry
    return mappings


def generate_wtc2_mappings() -> dict[str, dict]:
    """WTC Book 2: BWV 870-893, prelude + fugue each."""
    mappings = {}
    cycle = build_wtc_keys()
    for idx in range(24):
        bwv = 870 + idx
        tonic, mode = cycle[idx]
        entry = make_entry(tonic, mode)
        mappings[f"BWV{bwv}_prelude"] = entry
        mappings[f"BWV{bwv}_fugue"] = entry
    return mappings


def generate_goldberg_mappings() -> dict[str, dict]:
    """Goldberg Variations BWV 988: 32 variations (00-31).

    All G major except variations 15, 21, 25 which are G minor.
    """
    mappings = {}
    minor_variations = {15, 21, 25}
    for var_num in range(32):
        work_id = f"BWV988_{var_num:02d}"
        if var_num in minor_variations:
            mappings[work_id] = make_entry("G", "minor")
        else:
            mappings[work_id] = make_entry("G", "major")
    return mappings


def generate_cello_suite_mappings() -> dict[str, dict]:
    """Solo Cello Suites BWV 1007-1012, 6 movements each."""
    suite_keys = {
        1007: ("G", "major"),
        1008: ("D", "minor"),
        1009: ("C", "major"),
        1010: ("Eb", "major"),
        1011: ("C", "minor"),
        1012: ("D", "major"),
    }
    mappings = {}
    for bwv, (tonic, mode) in suite_keys.items():
        entry = make_entry(tonic, mode)
        for mvt in range(1, 7):
            mappings[f"BWV{bwv}_{mvt}"] = entry
    return mappings


def generate_solo_violin_mappings() -> dict[str, dict]:
    """Solo Violin Sonatas and Partitas BWV 1001-1006.

    BWV 1001: G minor (Sonata 1, 4 mvts)
    BWV 1003: A minor (Sonata 2, 4 mvts)
    BWV 1004: D minor (Partita 2, 5 mvts - includes Chaconne)
    BWV 1005: C major (Sonata 3, 4 mvts)
    BWV 1006: E major (Partita 3, mvts 1,3,4,5,6 - mvt 2 missing)
    """
    violin_keys: dict[int, tuple[str, str, list[int]]] = {
        1001: ("G", "minor", list(range(1, 5))),
        1003: ("A", "minor", list(range(1, 5))),
        1004: ("D", "minor", list(range(1, 6))),
        1005: ("C", "major", list(range(1, 5))),
        1006: ("E", "major", [1, 3, 4, 5, 6]),
    }
    mappings = {}
    for bwv, (tonic, mode, mvts) in violin_keys.items():
        entry = make_entry(tonic, mode)
        for mvt in mvts:
            mappings[f"BWV{bwv}_{mvt}"] = entry
    return mappings


def generate_trio_sonata_mappings() -> dict[str, dict]:
    """Trio Sonatas BWV 525-530, 3 movements each."""
    trio_keys = {
        525: ("Eb", "major"),
        526: ("C", "minor"),
        527: ("D", "minor"),
        528: ("E", "minor"),
        529: ("C", "major"),
        530: ("G", "major"),
    }
    mappings = {}
    for bwv, (tonic, mode) in trio_keys.items():
        entry = make_entry(tonic, mode)
        for mvt in range(1, 4):
            mappings[f"BWV{bwv}_{mvt}"] = entry
    return mappings


def generate_organ_prelude_fugue_mappings() -> dict[str, dict]:
    """Organ Prelude/Fugue pairs and related multi-movement organ works."""
    mappings = {}

    # Standard prelude + fugue pairs
    standard_pairs = {
        531: ("C", "major"),
        532: ("D", "major"),
        533: ("E", "minor"),
        534: ("F", "minor"),
        535: ("G", "minor"),
        536: ("A", "major"),
        539: ("D", "minor"),
        541: ("G", "major"),
        543: ("A", "minor"),
        544: ("B", "minor"),
        545: ("C", "major"),
        546: ("C", "minor"),
        547: ("C", "major"),
        548: ("E", "minor"),
        549: ("C", "minor"),
        552: ("Eb", "major"),
    }
    for bwv, (tonic, mode) in standard_pairs.items():
        entry = make_entry(tonic, mode)
        mappings[f"BWV{bwv}_prelude"] = entry
        mappings[f"BWV{bwv}_fugue"] = entry

    # Fantasia + fugue pairs
    fantasia_pairs = {
        537: ("C", "minor"),
        542: ("G", "minor"),
        562: ("C", "minor"),
    }
    for bwv, (tonic, mode) in fantasia_pairs.items():
        entry = make_entry(tonic, mode)
        mappings[f"BWV{bwv}_fantasia"] = entry
        mappings[f"BWV{bwv}_fugue"] = entry

    # Toccata + fugue pairs
    toccata_pairs = {
        538: ("D", "minor"),
        540: ("F", "major"),
    }
    for bwv, (tonic, mode) in toccata_pairs.items():
        entry = make_entry(tonic, mode)
        mappings[f"BWV{bwv}_toccata"] = entry
        mappings[f"BWV{bwv}_fugue"] = entry

    return mappings


def generate_standalone_organ_mappings() -> dict[str, dict]:
    """Standalone organ works (single file per BWV)."""
    standalone_keys = {
        550: ("G", "major"),
        551: ("A", "minor"),
        561: ("A", "minor"),
        565: ("D", "minor"),
        574: ("C", "minor"),
        575: ("C", "minor"),
        576: ("G", "major"),
        577: ("G", "major"),
        578: ("G", "minor"),
        579: ("B", "minor"),
        580: ("D", "major"),
        581: ("G", "major"),
        582: ("C", "minor"),
    }
    mappings = {}

    for bwv, (tonic, mode) in standalone_keys.items():
        # BWV 578 has a _fugue suffix in the reference data
        if bwv == 578:
            mappings[f"BWV{bwv}_fugue"] = make_entry(tonic, mode)
        else:
            mappings[f"BWV{bwv}"] = make_entry(tonic, mode)

    # BWV 552a/552b: St. Anne prelude and fugue as separate files
    entry_552 = make_entry("Eb", "major")
    mappings["BWV552a"] = entry_552
    mappings["BWV552b"] = entry_552

    return mappings


def generate_orgelbuchlein_mappings() -> dict[str, dict]:
    """Orgelbuchlein BWV 599-644. Confidence is 'inferred' (from chorale analysis)."""
    keys = {
        599: ("A", "minor"), 600: ("G", "major"), 601: ("G", "major"),
        602: ("F", "major"), 603: ("C", "major"), 604: ("G", "major"),
        605: ("D", "major"), 606: ("C", "major"), 607: ("D", "minor"),
        608: ("A", "major"), 609: ("C", "major"), 610: ("E", "minor"),
        611: ("D", "minor"), 612: ("G", "major"), 613: ("A", "major"),
        614: ("E", "minor"), 615: ("G", "major"), 616: ("D", "minor"),
        617: ("A", "minor"), 618: ("F", "major"), 619: ("F", "minor"),
        620: ("A", "minor"), 621: ("G", "minor"), 622: ("Eb", "major"),
        623: ("A", "minor"), 624: ("G", "minor"), 625: ("E", "minor"),
        626: ("D", "minor"), 627: ("D", "major"), 628: ("D", "major"),
        629: ("D", "major"), 630: ("D", "major"), 631: ("G", "major"),
        632: ("G", "major"), 633: ("A", "major"), 634: ("A", "major"),
        635: ("G", "major"), 636: ("D", "minor"), 637: ("A", "minor"),
        638: ("D", "major"), 639: ("F", "minor"), 640: ("E", "minor"),
        641: ("G", "major"), 642: ("A", "minor"), 643: ("A", "minor"),
        644: ("G", "minor"),
    }
    mappings = {}
    for bwv, (tonic, mode) in keys.items():
        mappings[f"BWV{bwv}"] = make_entry(tonic, mode, confidence="inferred")
    return mappings


def generate_schubler_mappings() -> dict[str, dict]:
    """Schubler Chorales BWV 645-650. Confidence is 'inferred'."""
    keys = {
        645: ("Eb", "major"),
        646: ("C", "minor"),
        647: ("C", "minor"),
        648: ("D", "minor"),
        649: ("Bb", "major"),
        650: ("G", "major"),
    }
    mappings = {}
    for bwv, (tonic, mode) in keys.items():
        mappings[f"BWV{bwv}"] = make_entry(tonic, mode, confidence="inferred")
    return mappings


def generate_lute_suite_mappings() -> dict[str, dict]:
    """Lute Suites BWV 995-998."""
    suite_keys: dict[int, tuple[str, str, int]] = {
        995: ("G", "minor", 6),
        996: ("E", "minor", 6),
        997: ("C", "minor", 4),
        998: ("Eb", "major", 3),
    }
    mappings = {}
    for bwv, (tonic, mode, num_mvts) in suite_keys.items():
        entry = make_entry(tonic, mode)
        for mvt in range(1, num_mvts + 1):
            mappings[f"BWV{bwv}_{mvt}"] = entry
    return mappings


def generate_leipzig_chorale_mappings() -> dict[str, dict]:
    """Leipzig Chorales BWV 651-667 ('Great Eighteen'). Confidence is 'inferred'."""
    keys = {
        651: ("G", "major"), 652: ("G", "major"), 653: ("G", "major"),
        654: ("Eb", "major"), 655: ("G", "major"), 656: ("A", "major"),
        657: ("G", "major"), 658: ("F", "minor"), 659: ("G", "minor"),
        660: ("G", "minor"), 661: ("G", "minor"), 662: ("A", "major"),
        663: ("G", "major"), 664: ("A", "major"), 665: ("E", "minor"),
        666: ("E", "minor"), 667: ("G", "major"),
    }
    mappings = {}
    for bwv, (tonic, mode) in keys.items():
        mappings[f"BWV{bwv}"] = make_entry(tonic, mode, confidence="inferred")
    return mappings


def generate_clavierubung3_mappings() -> dict[str, dict]:
    """Clavier-Übung III BWV 669-689. Confidence is 'inferred'."""
    keys = {
        669: ("C", "minor"), 670: ("Bb", "major"), 671: ("Bb", "major"),
        672: ("A", "minor"), 673: ("A", "minor"), 674: ("A", "minor"),
        675: ("F", "major"), 676: ("E", "minor"), 677: ("A", "major"),
        678: ("G", "major"), 679: ("G", "major"), 680: ("D", "minor"),
        681: ("E", "minor"), 682: ("E", "minor"), 683: ("D", "minor"),
        684: ("G", "minor"), 685: ("A", "minor"), 686: ("A", "minor"),
        687: ("B", "minor"), 688: ("D", "minor"), 689: ("F", "minor"),
    }
    mappings = {}
    for bwv, (tonic, mode) in keys.items():
        mappings[f"BWV{bwv}"] = make_entry(tonic, mode, confidence="inferred")
    return mappings


def generate_misc_chorale_mappings() -> dict[str, dict]:
    """Kirnberger and miscellaneous chorale preludes BWV 690-765.

    Confidence is 'inferred' (keys detected from pitch profile analysis).
    """
    # Standard BWV numbered chorales
    keys = {
        690: ("A", "minor"), 691: ("A", "minor"), 692: ("C", "major"),
        693: ("C", "major"), 694: ("G", "minor"), 695: ("D", "minor"),
        696: ("D", "minor"), 697: ("G", "major"), 698: ("G", "major"),
        699: ("G", "minor"), 700: ("G", "major"), 701: ("C", "major"),
        702: ("Bb", "major"), 703: ("F", "major"), 704: ("A", "minor"),
        705: ("D", "minor"),
        708: ("A", "major"),
        709: ("G", "major"), 710: ("G", "minor"), 711: ("G", "major"),
        712: ("A", "major"), 713: ("E", "minor"), 714: ("B", "major"),
        715: ("G", "major"), 716: ("G", "major"), 717: ("G", "major"),
        718: ("E", "minor"), 719: ("G", "major"), 720: ("D", "major"),
        721: ("B", "minor"), 722: ("G", "major"), 723: ("G", "major"),
        724: ("G", "major"), 725: ("A", "minor"), 726: ("G", "major"),
        727: ("B", "minor"), 728: ("C", "major"), 729: ("A", "major"),
        730: ("G", "major"), 731: ("G", "major"), 732: ("E", "major"),
        733: ("D", "minor"), 734: ("G", "major"), 735: ("Bb", "major"),
        736: ("D", "major"), 737: ("D", "minor"), 738: ("D", "major"),
        739: ("G", "major"), 740: ("F", "major"), 741: ("G", "minor"),
        742: ("B", "minor"), 743: ("A", "minor"), 744: ("A", "minor"),
        745: ("E", "minor"), 746: ("D", "minor"), 747: ("G", "minor"),
        748: ("D", "major"), 749: ("G", "major"), 750: ("D", "major"),
        751: ("G", "major"), 752: ("G", "major"), 753: ("D", "minor"),
        754: ("Bb", "major"), 755: ("G", "major"), 756: ("G", "major"),
        757: ("G", "major"), 758: ("G", "major"), 759: ("F", "major"),
        760: ("D", "minor"), 761: ("A", "minor"), 762: ("D", "minor"),
        763: ("D", "major"), 764: ("G", "major"), 765: ("D", "minor"),
    }
    mappings = {}
    for bwv, (tonic, mode) in keys.items():
        mappings[f"BWV{bwv}"] = make_entry(tonic, mode, confidence="inferred")

    # BWV 706 and 707 have a/b variants
    for suffix_bwv, tonic, mode in [
        ("706a", "A", "major"), ("706b", "A", "major"),
        ("707a", "A", "minor"), ("707b", "A", "minor"),
    ]:
        mappings[f"BWV{suffix_bwv}"] = make_entry(tonic, mode, confidence="inferred")

    return mappings


def main() -> int:
    """Generate key_signatures.json from all BWV mappings."""
    # Build all mappings
    all_mappings: dict[str, dict] = {}
    generators = [
        ("WTC1", generate_wtc1_mappings),
        ("WTC2", generate_wtc2_mappings),
        ("Goldberg Variations", generate_goldberg_mappings),
        ("Solo Cello Suites", generate_cello_suite_mappings),
        ("Solo Violin", generate_solo_violin_mappings),
        ("Trio Sonatas", generate_trio_sonata_mappings),
        ("Organ Prelude/Fugue", generate_organ_prelude_fugue_mappings),
        ("Standalone Organ", generate_standalone_organ_mappings),
        ("Orgelbuchlein", generate_orgelbuchlein_mappings),
        ("Schubler Chorales", generate_schubler_mappings),
        ("Lute Suites", generate_lute_suite_mappings),
        ("Leipzig Chorales", generate_leipzig_chorale_mappings),
        ("Clavier-Ubung III", generate_clavierubung3_mappings),
        ("Misc Chorales", generate_misc_chorale_mappings),
    ]

    for name, generator in generators:
        mappings = generator()
        print(f"  {name}: {len(mappings)} entries")
        # Check for duplicates
        for key in mappings:
            if key in all_mappings:
                print(f"  WARNING: Duplicate work_id '{key}' from {name}")
        all_mappings.update(mappings)

    print(f"\nTotal mappings: {len(all_mappings)}")

    # Read actual file list from data/reference/
    if not REFERENCE_DIR.exists():
        print(f"ERROR: Reference directory not found: {REFERENCE_DIR}")
        return 1

    actual_files = sorted([
        path.stem for path in REFERENCE_DIR.glob("*.json")
        if path.name != "key_signatures.json"
    ])
    print(f"Actual reference files: {len(actual_files)}")

    # Check for files missing from mapping
    missing_from_mapping = [fid for fid in actual_files if fid not in all_mappings]
    if missing_from_mapping:
        print(f"\nWARNING: {len(missing_from_mapping)} files MISSING from mapping:")
        for fid in missing_from_mapping:
            print(f"  - {fid}")

    # Check for mapping entries not in actual files
    extra_in_mapping = [wid for wid in all_mappings if wid not in actual_files]
    if extra_in_mapping:
        print(f"\nWARNING: {len(extra_in_mapping)} mapping entries have NO reference file:")
        for wid in sorted(extra_in_mapping):
            print(f"  - {wid}")

    # Sort by key and write
    sorted_mappings = dict(sorted(all_mappings.items()))

    OUTPUT_FILE.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT_FILE, "w", encoding="utf-8") as out_file:
        json.dump(sorted_mappings, out_file, indent=2, ensure_ascii=False)
        out_file.write("\n")  # trailing newline

    print(f"\nWrote {len(sorted_mappings)} entries to {OUTPUT_FILE}")

    # Summary
    if missing_from_mapping or extra_in_mapping:
        print("\nResult: WARNINGS present (see above)")
        return 1
    else:
        print(f"\nResult: All {len(sorted_mappings)} files mapped successfully")
        return 0


if __name__ == "__main__":
    sys.exit(main())
