#!/usr/bin/env python3
"""Drift guards for declarations that product code has stopped reaching.

A field or enumerator can be declared, documented, and completely dead: the
behaviour its name promises is never read by any composing code, so removing it
would change no note and no test would notice. Spotting that by hand is exactly
what nobody does, so these guards state the condition mechanically.

  struct fields   CharacterProfile (src/composer/character_profile.h)
                  Every field must be READ through a member access by a file
                  other than its own declaring pair, i.e. by a form builder,
                  the ornament pass, or another composing translation unit.

  enum values     SubjectCharacter / FormType (src/core/basic_types.h)
                  Every enumerator must reach a distinct outcome in the form
                  director: its own switch-case body, or an explicit rejection
                  by the form/character compatibility predicate. An enumerator
                  that only falls through to another value's body, or that the
                  director never names at all, produces output identical to
                  some other value and is reported.

Both guards search a directory tree and locate their subjects by symbol, never
by a fixed file path, so splitting a translation unit does not make an existing
declaration look absent. When a subject cannot be located at all the guard
raises ReachabilityError instead of reporting an empty, always-passing set.

Usage:
  bach-tools reachability            # human report, exit 1 on an unreached declaration
  bach-tools reachability --json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Iterable, Iterator

from bachlib.common import REPO_ROOT

DEFAULT_ROOT = REPO_ROOT / "src"
SOURCE_SUFFIXES = (".h", ".cpp", ".inc")

# The struct whose every field must have a product reader.
GUARDED_STRUCT = "CharacterProfile"
# The enums whose every value must reach a distinct outcome in the form director.
GUARDED_ENUMS = ("SubjectCharacter", "FormType")

# The form director is located by the symbols it defines rather than by file
# name, so a split of the unit keeps the guard pointed at the real code. Any
# .cpp defining one of these is treated as part of the director unit; a file
# that merely calls them is a caller, and its own switches must not stand in
# for the director's decision.
DIRECTOR_SYMBOLS = ("buildFormFixture", "isFormCharacterCompatible")

# A bool predicate whose name carries "compatible" is where a form/character
# combination is explicitly refused; the values it names are rejected outcomes.
COMPATIBILITY_DEF = re.compile(r"\bbool\s+(\w*[Cc]ompatible\w*)\s*\([^;{]*\{")


class ReachabilityError(RuntimeError):
    """A guarded declaration or its reading unit could not be located."""


# ---------------------------------------------------------------------------
# Source scanning
# ---------------------------------------------------------------------------


def iter_sources(root: Path) -> Iterator[Path]:
    """Yield every C++ source file under root, in a stable order."""
    for path in sorted(root.rglob("*")):
        if path.is_file() and path.suffix in SOURCE_SUFFIXES:
            yield path


def is_test_source(path: Path) -> bool:
    """True for test translation units, which never count as a product reader."""
    return "tests" in path.parts or path.stem.endswith("_test") or path.stem.startswith("test_")


def strip_comments_and_strings(text: str) -> str:
    """Blank out comments and literal contents, preserving offsets loosely.

    A field named only in a doc comment, and a value named only inside a string
    literal, are not reads. Comments collapse to a space and literal bodies are
    dropped so the remaining text is code only.
    """
    out: list[str] = []
    idx = 0
    size = len(text)
    while idx < size:
        two = text[idx : idx + 2]
        if two == "//":
            end = text.find("\n", idx)
            idx = size if end < 0 else end
            out.append(" ")
        elif two == "/*":
            end = text.find("*/", idx + 2)
            idx = size if end < 0 else end + 2
            out.append(" ")
        elif text[idx] == "R" and text[idx + 1 : idx + 2] == '"':
            # Raw string: R"delim( ... )delim"
            open_paren = text.find("(", idx + 2)
            if open_paren < 0:
                out.append(text[idx])
                idx += 1
                continue
            delim = text[idx + 2 : open_paren]
            close = text.find(")" + delim + '"', open_paren)
            idx = size if close < 0 else close + len(delim) + 2
            out.append('""')
        elif text[idx] in "\"'":
            quote = text[idx]
            idx += 1
            while idx < size and text[idx] != quote:
                idx += 2 if text[idx] == "\\" else 1
            idx += 1
            out.append('""')
        else:
            out.append(text[idx])
            idx += 1
    return "".join(out)


# Every guarded field and enum rescans the tree, so the stripped text of each
# file is kept keyed by its identity and mtime; a file rewritten in place is
# re-read rather than served stale.
_CODE_CACHE: dict[tuple[str, int, int], str] = {}


def read_code(path: Path) -> str:
    """Comment-stripped and literal-stripped text of one source file."""
    stat = path.stat()
    key = (str(path), stat.st_mtime_ns, stat.st_size)
    code = _CODE_CACHE.get(key)
    if code is None:
        code = strip_comments_and_strings(path.read_text(encoding="utf-8", errors="replace"))
        _CODE_CACHE[key] = code
    return code


def rel(path: Path, root: Path | None = None) -> str:
    """Display path, relative to the repo root or else to the searched tree."""
    for base in (REPO_ROOT, root):
        if base is None:
            continue
        try:
            return str(path.relative_to(base))
        except ValueError:
            continue
    return str(path)


def _matching_brace(text: str, open_idx: int) -> int:
    """Index of the brace closing the one at open_idx."""
    depth = 0
    for idx in range(open_idx, len(text)):
        if text[idx] == "{":
            depth += 1
        elif text[idx] == "}":
            depth -= 1
            if depth == 0:
                return idx
    raise ReachabilityError("unbalanced braces while reading a declaration body")


def _block_body(text: str, pattern: re.Pattern[str]) -> list[str]:
    """Bodies of every brace block whose header matches pattern."""
    bodies: list[str] = []
    for match in pattern.finditer(text):
        open_idx = text.index("{", match.start())
        bodies.append(text[open_idx + 1 : _matching_brace(text, open_idx)])
    return bodies


# ---------------------------------------------------------------------------
# Guard 1: struct field reachability
# ---------------------------------------------------------------------------


def find_struct_body(root: Path, struct_name: str) -> tuple[list[Path], str]:
    """Locate a struct definition anywhere under root.

    Returns the declaring files and the definition body. Raises when the struct
    is nowhere to be found, or is defined more than once (an ambiguous subject
    would let the guard check the wrong copy).
    """
    header = re.compile(r"\bstruct\s+" + re.escape(struct_name) + r"\b[^;{]*\{")
    found: list[tuple[Path, str]] = []
    for path in iter_sources(root):
        code = read_code(path)
        bodies = _block_body(code, header)
        for body in bodies:
            found.append((path, body))
    if not found:
        raise ReachabilityError(
            f"struct {struct_name} is not defined anywhere under {rel(root, root.parent)}; "
            "it was renamed, removed, or moved out of the searched tree - "
            "re-point this guard instead of letting it pass on an empty set"
        )
    if len(found) > 1:
        where = ", ".join(rel(path, root) for path, _ in found)
        raise ReachabilityError(
            f"struct {struct_name} is defined more than once ({where}); "
            "the guard cannot tell which definition product code reads"
        )
    path, body = found[0]
    return [path], body


def struct_field_names(body: str) -> list[str]:
    """Data member names declared directly in a struct body."""
    names: list[str] = []
    for statement in body.split(";"):
        stmt = statement.strip()
        if not stmt or "(" in stmt or "{" in stmt or "}" in stmt:
            continue
        if stmt.split(None, 1)[0] in ("using", "typedef", "friend", "enum", "struct", "class"):
            continue
        # Drop a default initializer and an array extent, then take the last
        # identifier, which is the member name.
        stmt = stmt.split("=", 1)[0]
        stmt = re.sub(r"\[[^\]]*\]", "", stmt)
        match = re.search(r"(\w+)\s*$", stmt.strip())
        if match and len(stmt.split()) >= 2:
            names.append(match.group(1))
    return names


def field_readers(root: Path, field: str, exclude_stems: Iterable[str]) -> list[str]:
    """Files that read the field through a member access.

    A member access (``.field`` / ``->field``) that is not the left side of a
    plain assignment counts as a read. The declaring pair and test sources are
    excluded, so only genuine product consumers are reported.
    """
    skip = set(exclude_stems)
    access = re.compile(r"(?:\.|->)\s*" + re.escape(field) + r"\b(?!\s*=[^=])")
    readers: list[str] = []
    for path in iter_sources(root):
        if path.stem in skip or is_test_source(path):
            continue
        if access.search(read_code(path)):
            readers.append(rel(path, root))
    return readers


def check_struct_fields(
    root: Path = DEFAULT_ROOT, struct_name: str = GUARDED_STRUCT
) -> dict[str, Any]:
    """Report which fields of the struct no product code reads."""
    declaring, body = find_struct_body(root, struct_name)
    fields = struct_field_names(body)
    if not fields:
        raise ReachabilityError(
            f"struct {struct_name} was found in {rel(declaring[0], root)} but no data member "
            "could be parsed out of it; the guard would pass vacuously"
        )
    stems = {path.stem for path in declaring}
    readers = {field: field_readers(root, field, stems) for field in fields}
    unread = [field for field, paths in readers.items() if not paths]
    return {
        "struct": struct_name,
        "declared_in": [rel(path, root) for path in declaring],
        "fields": fields,
        "readers": readers,
        "unread": unread,
        "ok": not unread,
    }


# ---------------------------------------------------------------------------
# Guard 2: enum value reachability in the form director
# ---------------------------------------------------------------------------


def find_enum_values(root: Path, enum_name: str) -> tuple[list[Path], list[str]]:
    """Enumerator names of an enum defined anywhere under root."""
    header = re.compile(r"\benum\s+(?:class\s+|struct\s+)?" + re.escape(enum_name) + r"\b[^;{]*\{")
    found: list[tuple[Path, str]] = []
    for path in iter_sources(root):
        for body in _block_body(read_code(path), header):
            found.append((path, body))
    if not found:
        raise ReachabilityError(
            f"enum {enum_name} is not defined anywhere under {rel(root, root.parent)}; "
            "it was renamed, removed, or moved out of the searched tree - "
            "re-point this guard instead of letting it pass on an empty set"
        )
    if len(found) > 1:
        where = ", ".join(rel(path, root) for path, _ in found)
        raise ReachabilityError(f"enum {enum_name} is defined more than once ({where})")
    path, body = found[0]
    values: list[str] = []
    for item in body.split(","):
        name = item.split("=", 1)[0].strip()
        if re.fullmatch(r"\w+", name):
            values.append(name)
    if not values:
        raise ReachabilityError(
            f"enum {enum_name} was found in {rel(path, root)} but declares no value the guard "
            "can read; the guard would pass vacuously"
        )
    return [path], values


def defines_symbol(code: str, symbol: str) -> bool:
    """True when the code defines (not merely calls) the named free function.

    A parameter list that itself contains a parenthesis is not recognized, which
    makes the guard fail loudly rather than mistake a call for a definition.
    """
    return re.search(re.escape(symbol) + r"\s*\([^;{()]*\)\s*(?:const\s*)?\{", code) is not None


def find_director_unit(root: Path) -> list[Path]:
    """Translation units that make the form/character dispatch decision."""
    unit = [
        path
        for path in iter_sources(root)
        if path.suffix == ".cpp"
        and not is_test_source(path)
        and any(defines_symbol(read_code(path), symbol) for symbol in DIRECTOR_SYMBOLS)
    ]
    if not unit:
        raise ReachabilityError(
            "no translation unit under "
            f"{rel(root, root.parent)} defines any of {', '.join(DIRECTOR_SYMBOLS)}; the form "
            "director was renamed or split away from these symbols - re-point this "
            "guard instead of letting it pass on an empty set"
        )
    return unit


def case_bodies(text: str, enum_name: str) -> dict[str, list[str]]:
    """Whitespace-normalized switch-case body per enumerator of one enum.

    A body ends at the next case/default label at the same brace depth or at the
    brace closing the switch, so a label that falls straight through to another
    label yields an empty body.
    """
    label = re.compile(r"\bcase\s+(?:\w+::)*" + re.escape(enum_name) + r"::(\w+)\s*:")
    next_label = re.compile(r"\bcase\b|\bdefault\b")
    bodies: dict[str, list[str]] = {}
    for match in label.finditer(text):
        idx = match.end()
        depth = 0
        while idx < len(text):
            char = text[idx]
            if char == "{":
                depth += 1
            elif char == "}":
                if depth == 0:
                    break
                depth -= 1
            elif depth == 0 and char in "cd" and next_label.match(text, idx):
                break
            idx += 1
        bodies.setdefault(match.group(1), []).append(" ".join(text[match.end() : idx].split()))
    return bodies


def rejected_values(text: str, enum_name: str) -> tuple[set[str], list[str]]:
    """Enumerators named by a compatibility predicate, plus the predicate names."""
    rejected: set[str] = set()
    predicates = [match.group(1) for match in COMPATIBILITY_DEF.finditer(text)]
    for body in _block_body(text, COMPATIBILITY_DEF):
        rejected.update(re.findall(r"\b" + re.escape(enum_name) + r"::(\w+)\b", body))
    return rejected, predicates


def check_enum_values(enum_name: str, root: Path = DEFAULT_ROOT) -> dict[str, Any]:
    """Report which enumerators the form director cannot tell apart.

    Each value is classified as ``dispatched`` (it owns a case body no other
    value of the same enum shares), ``rejected`` (a compatibility predicate
    refuses it explicitly), ``shared_outcome`` (its only case bodies are empty
    fall-through or byte-identical to another value's), or ``absent``.
    """
    declaring, values = find_enum_values(root, enum_name)
    unit = find_director_unit(root)
    code = "\n".join(read_code(path) for path in unit)

    bodies = case_bodies(code, enum_name)
    rejected, predicates = rejected_values(code, enum_name)
    if not bodies and not rejected:
        raise ReachabilityError(
            f"the form director unit ({', '.join(rel(path, root) for path in unit)}) never "
            f"names {enum_name}; the dispatch moved elsewhere - re-point this guard "
            "instead of reporting every value as unreachable"
        )

    # A body shared with another value is not a distinct outcome for either.
    body_owners: dict[str, set[str]] = {}
    for value, value_bodies in bodies.items():
        for body in value_bodies:
            if body:
                body_owners.setdefault(body, set()).add(value)

    sites: dict[str, str] = {}
    for value in values:
        distinct = any(
            body and body_owners.get(body, set()) == {value} for body in bodies.get(value, [])
        )
        if distinct:
            sites[value] = "dispatched"
        elif value in rejected:
            sites[value] = "rejected"
        elif value in bodies:
            sites[value] = "shared_outcome"
        else:
            sites[value] = "absent"

    unreachable = [value for value, kind in sites.items() if kind in ("shared_outcome", "absent")]
    return {
        "enum": enum_name,
        "declared_in": [rel(path, root) for path in declaring],
        "director_unit": [rel(path, root) for path in unit],
        "rejection_predicates": predicates,
        "values": values,
        "sites": sites,
        "unreachable": unreachable,
        "ok": not unreachable,
    }


# ---------------------------------------------------------------------------
# Report / CLI
# ---------------------------------------------------------------------------


def build_report(root: Path = DEFAULT_ROOT) -> dict[str, Any]:
    """Run both guards over one source tree."""
    struct_report = check_struct_fields(root)
    enum_reports = [check_enum_values(name, root) for name in GUARDED_ENUMS]
    return {
        "root": rel(root, root.parent),
        "struct_fields": struct_report,
        "enum_values": enum_reports,
        "ok": struct_report["ok"] and all(report["ok"] for report in enum_reports),
    }


def _add_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--json", action="store_true", help="emit JSON instead of text")
    parser.add_argument("--out", type=Path, help="write the JSON report to this path")
    parser.add_argument(
        "--root", type=Path, default=DEFAULT_ROOT, help="source tree to search (default: src/)"
    )


def register(subparsers) -> None:
    """Register the ``reachability`` subcommand on a subparser action."""
    parser = subparsers.add_parser(
        "reachability",
        help="check that declared fields and enum values are still reached by product code",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Execute both reachability guards from parsed CLI args."""
    try:
        report = build_report(args.root)
    except ReachabilityError as exc:
        sys.stderr.write(f"reachability guard cannot run: {exc}\n")
        return 2

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

    if args.json:
        sys.stdout.write(json.dumps(report, indent=2) + "\n")
        return 0 if report["ok"] else 1

    struct_report = report["struct_fields"]
    sys.stdout.write(f"{struct_report['struct']} fields read by product code\n")
    for field in struct_report["fields"]:
        readers = struct_report["readers"][field]
        detail = f"{len(readers)} reader(s)" if readers else "NO PRODUCT READER"
        sys.stdout.write(f"  {field:<22} {detail}\n")
    for enum_report in report["enum_values"]:
        sys.stdout.write(f"{enum_report['enum']} values reached by the form director\n")
        for value in enum_report["values"]:
            sys.stdout.write(f"  {value:<22} {enum_report['sites'][value]}\n")

    if not report["ok"]:
        sys.stdout.write("\nUNREACHED DECLARATIONS:\n")
        if struct_report["unread"]:
            sys.stdout.write(
                f"  {struct_report['struct']} fields with no product reader: "
                f"{', '.join(struct_report['unread'])}\n"
            )
        for enum_report in report["enum_values"]:
            if enum_report["unreachable"]:
                sys.stdout.write(
                    f"  {enum_report['enum']} values with no distinct outcome: "
                    f"{', '.join(enum_report['unreachable'])}\n"
                )
    return 0 if report["ok"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    _add_arguments(parser)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
