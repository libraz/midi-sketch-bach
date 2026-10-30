#!/usr/bin/env python3
"""Validate generated.json against the vendored generated.v1 schema.

Minimal Draft 2020-12 subset validator. Only supports the features
actually used by the vendored ``schema/generated.v1.json``:

    - object: type, required, additionalProperties=false, properties
    - array : type, items
    - integer: type, minimum, maximum
    - string : type, const, enum
    - any    : list-valued type (e.g. ["integer", "null"])

Any other keyword is a usage error (exit 2) rather than a silent pass.

External ``jsonschema`` package is intentionally NOT required so this
script stays usable from CI without extra pip installs.

The schema path defaults to the vendored ``schema/generated.v1.json``
in this repository. Exits 0 on pass, 1 on schema mismatch, 2 on
IO / parse error.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any

from bachlib.common import REPO_ROOT

# The wire contract ships with this repository so validation never depends on
# a sibling checkout or silently skips in CI.
DEFAULT_SCHEMA = REPO_ROOT / "schema" / "generated.v1.json"


class UnsupportedSchemaError(Exception):
    """Raised when the schema uses a keyword or type this subset cannot evaluate."""


# Keywords the validator evaluates, plus pure annotations that carry no constraint.
SUPPORTED_KEYWORDS = frozenset(
    {
        "type",
        "required",
        "additionalProperties",
        "properties",
        "items",
        "minimum",
        "maximum",
        "const",
        "enum",
    }
)
ANNOTATION_KEYWORDS = frozenset({"$schema", "$id", "title", "description"})
SUPPORTED_TYPES = frozenset({"object", "array", "integer", "string", "null"})


def _matches_type(node: Any, type_name: str) -> bool:
    """Whether ``node`` is an instance of the JSON Schema type ``type_name``."""
    if type_name == "object":
        return isinstance(node, dict)
    if type_name == "array":
        return isinstance(node, list)
    if type_name == "integer":
        return isinstance(node, int) and not isinstance(node, bool)
    if type_name == "string":
        return isinstance(node, str)
    return node is None


def check_schema(schema: dict[str, Any], path: str = "$") -> None:
    """Statically walk every subschema and reject keywords or types outside the subset.

    Independent of any validated data, so an unsupported keyword under an optional
    property is caught even when that property is absent.

    @param schema The schema fragment to inspect.
    @param path Dotted/indexed schema path used for error messages.
    @throws UnsupportedSchemaError When any subschema uses a keyword or type outside the subset.
    """
    for keyword in schema:
        if keyword not in SUPPORTED_KEYWORDS and keyword not in ANNOTATION_KEYWORDS:
            raise UnsupportedSchemaError(f"{path}: unsupported schema keyword '{keyword}'")
    declared = schema.get("type")
    for type_name in [declared] if isinstance(declared, str) else list(declared or []):
        if type_name not in SUPPORTED_TYPES:
            raise UnsupportedSchemaError(f"{path}: unsupported schema type '{type_name}'")
    for key, sub_schema in schema.get("properties", {}).items():
        check_schema(sub_schema, f"{path}.{key}")
    if isinstance(schema.get("items"), dict):
        check_schema(schema["items"], f"{path}[]")


def validate(node: Any, schema: dict[str, Any], path: str, errors: list[str]) -> None:
    """Recursively validate ``node`` against the schema subset.

    @param node The JSON value under inspection.
    @param schema The schema fragment that ``node`` must satisfy.
    @param path Dotted/indexed JSON path used for error messages.
    @param errors Accumulator that receives one message per violation.
    @throws UnsupportedSchemaError When the schema uses a keyword or type outside the subset.
    """
    for keyword in schema:
        if keyword not in SUPPORTED_KEYWORDS and keyword not in ANNOTATION_KEYWORDS:
            raise UnsupportedSchemaError(f"{path}: unsupported schema keyword '{keyword}'")

    declared = schema.get("type")
    allowed = [declared] if isinstance(declared, str) else list(declared or [])
    for type_name in allowed:
        if type_name not in SUPPORTED_TYPES:
            raise UnsupportedSchemaError(f"{path}: unsupported schema type '{type_name}'")

    if allowed:
        matched = [name for name in allowed if _matches_type(node, name)]
        if not matched:
            errors.append(
                f"{path}: expected {' or '.join(allowed)}, got {type(node).__name__}"
            )
            return
        active = matched[0]
    else:
        active = None

    if active == "object":
        for required in schema.get("required", []):
            if required not in node:
                errors.append(f"{path}: missing required key '{required}'")
        props = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            for key in node.keys():
                if key not in props:
                    errors.append(f"{path}: unexpected key '{key}'")
        for key, sub_schema in props.items():
            if key in node:
                validate(node[key], sub_schema, f"{path}.{key}", errors)
    elif active == "array":
        item_schema = schema.get("items")
        if item_schema is not None:
            for i, item in enumerate(node):
                validate(item, item_schema, f"{path}[{i}]", errors)
    elif active == "integer":
        if "minimum" in schema and node < schema["minimum"]:
            errors.append(f"{path}: {node} < minimum {schema['minimum']}")
        if "maximum" in schema and node > schema["maximum"]:
            errors.append(f"{path}: {node} > maximum {schema['maximum']}")

    if "const" in schema:
        if node != schema["const"]:
            errors.append(f"{path}: expected const {schema['const']!r}, got {node!r}")

    if "enum" in schema:
        if node not in schema["enum"]:
            errors.append(f"{path}: {node!r} is not one of {schema['enum']!r}")


def _add_arguments(parser) -> None:
    """Register the ``validate`` command arguments on ``parser``."""
    parser.add_argument("output", type=Path, help="generated.v1 JSON to validate")
    parser.add_argument(
        "schema",
        type=Path,
        nargs="?",
        default=None,
        help="schema JSON (defaults to schema/generated.v1.json)",
    )


def register(subparsers) -> None:
    """Register the ``validate`` subcommand on a subparsers object."""
    parser = subparsers.add_parser(
        "validate",
        help="validate generated.v1 JSON against the vendored schema",
        description=__doc__,
    )
    _add_arguments(parser)
    parser.set_defaults(func=run)


def run(args) -> int:
    """Run schema validation for parsed CLI ``args``.

    @param args Namespace with ``output`` and optional ``schema`` paths.
    @return 0 on pass, 1 on schema mismatch, 2 on IO / parse error.
    """
    output_path: Path = args.output
    schema_path: Path = args.schema if args.schema is not None else DEFAULT_SCHEMA

    if not schema_path.is_file():
        print(f"schema not found: {schema_path}", file=sys.stderr)
        return 2
    if not output_path.is_file():
        print(f"output not found: {output_path}", file=sys.stderr)
        return 2

    try:
        with schema_path.open("r", encoding="utf-8") as handle:
            schema = json.load(handle)
        with output_path.open("r", encoding="utf-8") as handle:
            payload = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"parse error: {exc}", file=sys.stderr)
        return 2

    errors: list[str] = []
    try:
        check_schema(schema)
        validate(payload, schema, "$", errors)
    except UnsupportedSchemaError as exc:
        print(f"unsupported schema: {exc}", file=sys.stderr)
        return 2

    if errors:
        print(f"schema validation FAILED for {output_path}", file=sys.stderr)
        for msg in errors:
            print(f"  - {msg}", file=sys.stderr)
        return 1

    print(f"schema validation OK ({output_path} against {schema_path.name})")
    return 0


def main(argv: list[str]) -> int:
    """Standalone entry point preserving the historical exit-code contract.

    @param argv Argument list excluding the program name.
    @return 0 on pass, 1 on schema mismatch, 2 on IO / parse / usage error.
    """
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__, file=sys.stderr)
        return 2

    output = Path(argv[0])
    schema = Path(argv[1]) if len(argv) >= 2 else None

    class _Args:
        pass

    args = _Args()
    args.output = output
    args.schema = schema
    return run(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
