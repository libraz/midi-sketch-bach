#!/usr/bin/env python3
"""Validate generated.json against the bach-mcp schema.

Minimal Draft 2020-12 subset validator. Only supports the features
actually used by ``bach-mcp/schema/generated.v1.json``:

    - object: type, required, additionalProperties=false, properties
    - array : type, items
    - integer: type, minimum, maximum
    - string : type, const

External ``jsonschema`` package is intentionally NOT required so this
script stays usable from CI without extra pip installs.

Usage:
    validate_generated_json.py <output.json> [<schema.json>]

The schema path defaults to ``../bach-mcp/schema/generated.v1.json``
relative to this script. Exits 0 on pass, 1 on schema mismatch, 2 on
IO / parse error.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


def validate(node, schema, path: str, errors: list[str]) -> None:
    expected_type = schema.get("type")
    if expected_type == "object":
        if not isinstance(node, dict):
            errors.append(f"{path}: expected object, got {type(node).__name__}")
            return
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
        return

    if expected_type == "array":
        if not isinstance(node, list):
            errors.append(f"{path}: expected array, got {type(node).__name__}")
            return
        item_schema = schema.get("items")
        if item_schema is not None:
            for i, item in enumerate(node):
                validate(item, item_schema, f"{path}[{i}]", errors)
        return

    if expected_type == "integer":
        if isinstance(node, bool) or not isinstance(node, int):
            errors.append(f"{path}: expected integer, got {type(node).__name__}")
            return
        if "minimum" in schema and node < schema["minimum"]:
            errors.append(f"{path}: {node} < minimum {schema['minimum']}")
        if "maximum" in schema and node > schema["maximum"]:
            errors.append(f"{path}: {node} > maximum {schema['maximum']}")
        return

    if expected_type == "string":
        if not isinstance(node, str):
            errors.append(f"{path}: expected string, got {type(node).__name__}")
            return

    if "const" in schema:
        if node != schema["const"]:
            errors.append(f"{path}: expected const {schema['const']!r}, got {node!r}")


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__, file=sys.stderr)
        return 2

    output_path = Path(argv[1])
    if len(argv) >= 3:
        schema_path = Path(argv[2])
    else:
        schema_path = (
            Path(__file__).resolve().parent.parent.parent
            / "bach-mcp"
            / "schema"
            / "generated.v1.json"
        )

    if not schema_path.is_file():
        print(f"schema not found: {schema_path}", file=sys.stderr)
        return 2
    if not output_path.is_file():
        print(f"output not found: {output_path}", file=sys.stderr)
        return 2

    try:
        with schema_path.open("r", encoding="utf-8") as f:
            schema = json.load(f)
        with output_path.open("r", encoding="utf-8") as f:
            payload = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"parse error: {exc}", file=sys.stderr)
        return 2

    errors: list[str] = []
    validate(payload, schema, "$", errors)

    if errors:
        print(f"schema validation FAILED for {output_path}", file=sys.stderr)
        for msg in errors:
            print(f"  - {msg}", file=sys.stderr)
        return 1

    print(f"schema validation OK ({output_path} against {schema_path.name})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
