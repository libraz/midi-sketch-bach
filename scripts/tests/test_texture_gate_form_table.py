"""Drift guards: per-form facts have one definition, every surface agrees.

The C++ form definitions (basic_types.cpp, form_director.cpp) are the source of
truth. The texture gate's voice table, the demo's form table, the READMEs and
the TypeScript name unions are read here and compared against them, so a form
fact edited on one surface fails a test instead of drifting silently.
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
REPO_DIR = SCRIPTS_DIR.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import texture_gate  # noqa: E402

BASIC_TYPES = (REPO_DIR / "src/core/basic_types.cpp").read_text(encoding="utf-8")
FORM_DIRECTOR = (REPO_DIR / "src/composer/form_director.cpp").read_text(encoding="utf-8")
CLI_MAIN = (REPO_DIR / "src/cli_main.cpp").read_text(encoding="utf-8")
DEMO = (REPO_DIR / "demo/index.html").read_text(encoding="utf-8")
README_EN = (REPO_DIR / "README.md").read_text(encoding="utf-8")
README_JA = (REPO_DIR / "README_ja.md").read_text(encoding="utf-8")
TYPES_TS = (REPO_DIR / "js/src/types.ts").read_text(encoding="utf-8")


def _snake(camel: str) -> str:
    return re.sub(r"(?<!^)(?=[A-Z])", "_", camel).lower()


def _function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}\n", start)
    return source[start:end]


def _enum_names(function: str) -> list[str]:
    """Names returned by a `case Enum::X: return "name";` switch, in order."""
    body = _function_body(BASIC_TYPES, function)
    return re.findall(r'case \w+::\w+:\s*return "([^"]+)";', body)


def _form_names() -> list[str]:
    return _enum_names("const char* formTypeToString(")


def cpp_form_voices() -> dict[str, int]:
    """Voice count per form from the kFormSpecs table in form_director.cpp."""
    table = FORM_DIRECTOR[FORM_DIRECTOR.index("kFormSpecs") :]
    rows = re.findall(r"\{(\d+),[^}]*\},\s*//\s*(\w+)", table)
    return {_snake(name): int(voices) for voices, name in rows[: len(_form_names())]}


def cpp_form_instruments() -> dict[str, set[str]]:
    """Instruments each form accepts, from isInstrumentCompatibleWithForm."""
    body = _function_body(BASIC_TYPES, "bool isInstrumentCompatibleWithForm(")
    enumerators = re.findall(
        r"case InstrumentType::(\w+):",
        _function_body(BASIC_TYPES, "const char* instrumentTypeToString("),
    )
    instrument_names = dict(
        zip(enumerators, _enum_names("const char* instrumentTypeToString("))
    )
    accepted: dict[str, set[str]] = {}
    for labels, expression in re.findall(
        r"((?:\s*case FormType::\w+:)+)\s*return ([^;]+);", body
    ):
        instruments = {
            instrument_names[enumerator]
            for enumerator in re.findall(r"InstrumentType::(\w+)", expression)
        }
        for enumerator in re.findall(r"FormType::(\w+)", labels):
            accepted[_snake(enumerator)] = instruments
    return accepted


def demo_forms() -> dict[str, dict[str, object]]:
    """Rows of the demo FORMS table: voices and accepted instruments per form."""
    block = DEMO[DEMO.index("const FORMS = {") : DEMO.index("const formsInSystem")]
    rows: dict[str, dict[str, object]] = {}
    for name, voices, instruments in re.findall(
        r"'(\w+)': \{\s*system: '\w+', voices: (\d+), instruments: \[([^\]]*)\],", block
    ):
        rows[name] = {
            "voices": int(voices),
            "instruments": set(re.findall(r"'(\w+)'", instruments)),
        }
    return rows


def readme_instruments(text: str) -> dict[str, set[str]]:
    """Per-form accepted instruments from the README instrument table."""
    start = text.index("| `fugue`, `prelude_and_fugue`")
    accepted: dict[str, set[str]] = {}
    for line in text[start:].splitlines():
        if not line.startswith("|"):
            break
        form_cell, instrument_cell = [cell.strip() for cell in line.strip("|").split("|")]
        for form in re.findall(r"`(\w+)`", form_cell):
            accepted[form] = set(re.findall(r"`(\w+)`", instrument_cell))
    return accepted


def ts_union(name: str) -> list[str]:
    match = re.search(rf"export type {name} =([^;]+);", TYPES_TS)
    assert match is not None, name
    return re.findall(r"'([^']+)'", match.group(1))


class CppTablesParseTest(unittest.TestCase):
    def test_parsers_see_every_shipped_form(self) -> None:
        forms = _form_names()
        self.assertEqual(len(forms), 10)
        self.assertEqual(set(cpp_form_voices()), set(forms))
        self.assertEqual(set(cpp_form_instruments()), set(forms))
        self.assertEqual(set(demo_forms()), set(forms))


class FormVoiceTableTest(unittest.TestCase):
    def test_gate_voice_table_equals_the_core_form_table(self) -> None:
        self.assertEqual(texture_gate.FORM_EXPECTED_VOICES, cpp_form_voices())

    def test_demo_voice_counts_equal_the_core_form_table(self) -> None:
        demo_voices = {name: row["voices"] for name, row in demo_forms().items()}
        self.assertEqual(demo_voices, cpp_form_voices())

    def test_gate_forms_follow_the_core_form_order(self) -> None:
        self.assertEqual(list(texture_gate.SHIPPED_FORMS), _form_names())


class InstrumentTableTest(unittest.TestCase):
    def test_demo_instruments_equal_the_core_compatibility_table(self) -> None:
        demo_instruments = {name: row["instruments"] for name, row in demo_forms().items()}
        self.assertEqual(demo_instruments, cpp_form_instruments())

    def test_readme_instrument_tables_equal_the_core_compatibility_table(self) -> None:
        for label, text in (("README.md", README_EN), ("README_ja.md", README_JA)):
            with self.subTest(readme=label):
                self.assertEqual(readme_instruments(text), cpp_form_instruments())

    def test_no_surface_advertises_an_instrument_no_form_accepts(self) -> None:
        accepted = set().union(*cpp_form_instruments().values())
        enumerated = set(_enum_names("const char* instrumentTypeToString("))
        reserved = enumerated - accepted
        self.assertEqual(reserved, {"guitar"})
        for label, text in (
            ("README.md", README_EN),
            ("README_ja.md", README_JA),
            ("demo/index.html", DEMO),
        ):
            for name in reserved:
                self.assertNotIn(name, text.lower(), label)
        help_line = next(line for line in CLI_MAIN.splitlines() if "--instrument INS" in line)
        for name in reserved:
            self.assertNotIn(name, help_line)

    def test_typescript_names_equal_the_core_names(self) -> None:
        self.assertEqual(ts_union("FormName"), _form_names())
        self.assertEqual(
            ts_union("InstrumentName"), _enum_names("const char* instrumentTypeToString(")
        )
        self.assertEqual(
            ts_union("CharacterName"), _enum_names("const char* subjectCharacterToString(")
        )


class CharacterNameTest(unittest.TestCase):
    def test_character_names_are_lowercase_tokens(self) -> None:
        names = _enum_names("const char* subjectCharacterToString(")
        self.assertEqual(len(names), 4)
        for name in names:
            self.assertRegex(name, r"^[a-z]+$")

    def test_readme_lists_the_enumerated_characters(self) -> None:
        names = _enum_names("const char* subjectCharacterToString(")
        for label, text in (("README.md", README_EN), ("README_ja.md", README_JA)):
            row = next(line for line in text.splitlines() if line.startswith("| `--character CH`"))
            self.assertEqual(re.findall(r"`(\w+)`", row)[:4], names, label)


class DocumentedClaimsTest(unittest.TestCase):
    def test_readmes_name_the_cli_default_form(self) -> None:
        match = re.search(r"FormType form = (?:bach::)?FormType::(\w+);", CLI_MAIN)
        assert match is not None
        default_form = _snake(match.group(1))
        for label, text in (("README.md", README_EN), ("README_ja.md", README_JA)):
            row = next(line for line in text.splitlines() if line.startswith("| `--form FORM`"))
            self.assertIn(f"`{default_form}`", row, label)

        quick_en = next(
            line for line in README_EN.splitlines() if line.startswith("./build/bin/bach_cli ")
            and "--" not in line
        )
        quick_ja = next(
            line for line in README_JA.splitlines() if line.startswith("./build/bin/bach_cli ")
            and "--" not in line
        )
        self.assertRegex(quick_en, r"# Fugue in C major$")
        self.assertRegex(quick_ja, r"# ハ長調のフーガ$")

    def test_goldberg_variation_count_is_stated_once_everywhere(self) -> None:
        for label, text in (
            ("README.md", README_EN),
            ("README_ja.md", README_JA),
            ("demo/index.html", DEMO),
        ):
            self.assertNotIn("32-variation", text, label)
            self.assertNotIn("32 variations", text, label)
        self.assertIn("30 variations", README_EN)
        self.assertIn("30変奏", README_JA)
        self.assertIn("30 variations", str(demo_goldberg_description()))

    def test_fugue_device_claims_are_conditional_in_both_readmes(self) -> None:
        self.assertNotIn("Every fugue builds an exposition, real/tonal answer", README_EN)
        self.assertIn("only when the development has room", README_EN)
        self.assertIn("展開部に余地が", README_JA)


class DemoCharacterTest(unittest.TestCase):
    def test_character_is_selectable_and_sent_for_every_form(self) -> None:
        select = re.search(r'<select v-model\.number="config\.characterId"([^>]*)>', DEMO)
        assert select is not None
        self.assertNotIn("isOrganForm", select.group(1))
        send = DEMO.index("cfg.character = this.config.characterId;")
        self.assertNotIn("isOrganForm", DEMO[send - 160 : send])

    def test_character_hints_describe_one_global_profile(self) -> None:
        self.assertNotIn("Organ forms only", DEMO)
        self.assertNotIn("Character set per variation", DEMO)
        self.assertIn("One character profile for the whole piece", DEMO)


def demo_goldberg_description() -> str:
    match = re.search(r"'goldberg_variations': \{[^}]*description: '([^']*)'", DEMO)
    assert match is not None
    return match.group(1)


if __name__ == "__main__":
    unittest.main()
