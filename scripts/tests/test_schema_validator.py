"""Tests for the generated.v1 subset schema validator."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import schema  # noqa: E402


def check(node, fragment) -> list[str]:
    errors: list[str] = []
    schema.validate(node, fragment, "$", errors)
    return errors


class ListValuedTypeTest(unittest.TestCase):
    FRAGMENT = {"type": ["integer", "null"], "minimum": 0}

    def test_accepts_each_listed_type(self) -> None:
        self.assertEqual(check(3, self.FRAGMENT), [])
        self.assertEqual(check(None, self.FRAGMENT), [])

    def test_rejects_a_type_outside_the_list(self) -> None:
        self.assertTrue(check("x", self.FRAGMENT))
        self.assertTrue(check(True, self.FRAGMENT))

    def test_constraints_apply_to_the_matched_type(self) -> None:
        self.assertTrue(check(-1, self.FRAGMENT))


class UnsupportedKeywordTest(unittest.TestCase):
    def test_unknown_keyword_raises(self) -> None:
        with self.assertRaises(schema.UnsupportedSchemaError):
            check(1, {"type": "integer", "multipleOf": 2})

    def test_unknown_type_raises(self) -> None:
        with self.assertRaises(schema.UnsupportedSchemaError):
            check(1.5, {"type": "number"})

    def test_nested_unknown_keyword_raises(self) -> None:
        with self.assertRaises(schema.UnsupportedSchemaError):
            check([1], {"type": "array", "items": {"type": "integer", "pattern": "x"}})

    def test_vendored_schema_uses_only_supported_keywords(self) -> None:
        import json

        doc = json.loads(schema.DEFAULT_SCHEMA.read_text(encoding="utf-8"))
        errors: list[str] = []
        schema.validate({}, doc, "$", errors)  # must not raise
        self.assertTrue(errors)


if __name__ == "__main__":
    unittest.main()
