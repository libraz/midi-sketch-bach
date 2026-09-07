"""Tests for the declared-but-unreached drift guards.

The load-bearing cases are the negative ones: each guard is fed a synthetic
source tree in which a field has no product reader, or an enum value shares
another value's outcome, and must name that declaration. Without them a guard
that always passes would look identical to a guard that works.

The live-tree cases pin the current source: every CharacterProfile field has a
product reader and every SubjectCharacter / FormType value reaches a distinct
outcome in the form director.
"""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent.parent
if str(SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPTS_DIR))

from bachlib import reachability as rch  # noqa: E402

# A minimal director translation unit: an explicit rejection predicate plus a
# dispatch switch. Severe and Noble own distinct bodies, Playful is only ever
# rejected, and Restless is named nowhere.
DIRECTOR_CPP = """
namespace bach::composer {

bool isFormCharacterCompatible(FormType form, SubjectCharacter character) {
  if (form == FormType::ChoralePrelude) {
    if (character == SubjectCharacter::Playful)
      return false;
  }
  return true;
}

Tick characterSeparation(SubjectCharacter character) {
  switch (character) {
    case SubjectCharacter::Severe:
      return 40;
    case SubjectCharacter::Noble:
      return 24;
  }
  return 40;
}

FormDirectorStatus buildFormFixture(const ComposeRequest& req, HarnessFixture* out) {
  if (!isFormCharacterCompatible(req.form, req.character))
    return FormDirectorStatus::IncompatibleCharacter;
  return FormDirectorStatus::Ok;
}

}  // namespace bach::composer
"""

CHARACTER_ENUM_H = """
namespace bach {
enum class SubjectCharacter : uint8_t { Severe, Playful, Noble, Restless };
}  // namespace bach
"""


def write_tree(root: Path, files: dict[str, str]) -> None:
    """Materialize a synthetic source tree."""
    for name, content in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")


class SyntheticTreeCase(unittest.TestCase):
    """Base class giving each test its own throwaway source tree."""

    def build(self, files: dict[str, str]) -> Path:
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name) / "src"
        write_tree(root, files)
        return root


class StructFieldParsingTest(unittest.TestCase):
    def test_reads_field_names_and_skips_methods(self) -> None:
        body = """
          std::int8_t density_bias;
          bool prefer_dotted;
          std::uint8_t ornament_density;
          std::uint8_t tail[4];
          int scaled(int tier) const;
        """
        self.assertEqual(
            rch.struct_field_names(body),
            ["density_bias", "prefer_dotted", "ornament_density", "tail"],
        )

    def test_comment_and_literal_stripping(self) -> None:
        stripped = rch.strip_comments_and_strings(
            'int a;  // .prefer_dotted\n/* .ornament_density */ const char* n = ".density_bias";'
        )
        self.assertNotIn("prefer_dotted", stripped)
        self.assertNotIn("ornament_density", stripped)
        self.assertNotIn("density_bias", stripped)
        self.assertIn("int a;", stripped)


class StructFieldGuardNegativeTest(SyntheticTreeCase):
    """A field with no product reader must be named by the guard."""

    def profile_header(self) -> str:
        return """
          namespace bach::composer::detail {
          struct CharacterProfile {
            std::int8_t density_bias;
            bool prefer_syncopation;
          };
          }  // namespace bach::composer::detail
        """

    def test_field_with_no_reader_is_reported(self) -> None:
        root = self.build(
            {
                "composer/character_profile.h": self.profile_header(),
                "composer/form_fugue.cpp": "int t = profile.density_bias;",
            }
        )
        report = rch.check_struct_fields(root)
        self.assertFalse(report["ok"])
        self.assertEqual(report["unread"], ["prefer_syncopation"])
        self.assertEqual(report["readers"]["density_bias"], ["composer/form_fugue.cpp"])

    def test_mention_in_a_comment_is_not_a_reader(self) -> None:
        root = self.build(
            {
                "composer/character_profile.h": self.profile_header(),
                "composer/form_fugue.cpp": (
                    "// Noble (profile.prefer_syncopation) syncopates the answer.\n"
                    "int t = profile.density_bias;"
                ),
            }
        )
        self.assertEqual(rch.check_struct_fields(root)["unread"], ["prefer_syncopation"])

    def test_declaring_pair_is_not_a_reader(self) -> None:
        root = self.build(
            {
                "composer/character_profile.h": self.profile_header(),
                "composer/character_profile.cpp": (
                    "bool syncopates(const CharacterProfile& p) { return p.prefer_syncopation; }"
                ),
                "composer/form_fugue.cpp": "int t = profile.density_bias;",
            }
        )
        self.assertEqual(rch.check_struct_fields(root)["unread"], ["prefer_syncopation"])

    def test_test_source_is_not_a_reader(self) -> None:
        root = self.build(
            {
                "composer/character_profile.h": self.profile_header(),
                "composer/form_fugue.cpp": "int t = profile.density_bias;",
                "tests/character_profile_test.cpp": "EXPECT_TRUE(profile.prefer_syncopation);",
            }
        )
        self.assertEqual(rch.check_struct_fields(root)["unread"], ["prefer_syncopation"])

    def test_write_only_field_is_not_read(self) -> None:
        root = self.build(
            {
                "composer/character_profile.h": self.profile_header(),
                "composer/form_fugue.cpp": (
                    "profile.prefer_syncopation = true;\nint t = profile.density_bias;"
                ),
            }
        )
        self.assertEqual(rch.check_struct_fields(root)["unread"], ["prefer_syncopation"])

    def test_comparison_counts_as_a_read(self) -> None:
        root = self.build(
            {
                "composer/character_profile.h": self.profile_header(),
                "composer/form_fugue.cpp": (
                    "if (profile.prefer_syncopation == true) {}\nint t = profile.density_bias;"
                ),
            }
        )
        report = rch.check_struct_fields(root)
        self.assertTrue(report["ok"], report["unread"])


class StructFieldGuardLoudFailureTest(SyntheticTreeCase):
    """A guard that cannot find its subject must fail, not pass on an empty set."""

    def test_missing_struct_raises(self) -> None:
        root = self.build({"composer/form_fugue.cpp": "int t = 0;"})
        with self.assertRaises(rch.ReachabilityError) as ctx:
            rch.check_struct_fields(root)
        self.assertIn("CharacterProfile", str(ctx.exception))

    def test_duplicate_definition_raises(self) -> None:
        struct = "struct CharacterProfile { std::int8_t density_bias; };"
        root = self.build(
            {"composer/character_profile.h": struct, "composer/other_profile.h": struct}
        )
        with self.assertRaises(rch.ReachabilityError) as ctx:
            rch.check_struct_fields(root)
        self.assertIn("more than once", str(ctx.exception))

    def test_struct_without_parsable_fields_raises(self) -> None:
        root = self.build({"composer/character_profile.h": "struct CharacterProfile {};"})
        with self.assertRaises(rch.ReachabilityError) as ctx:
            rch.check_struct_fields(root)
        self.assertIn("vacuously", str(ctx.exception))

    def test_struct_found_after_a_file_split(self) -> None:
        # The declaration moved to a differently named header; locating it by
        # symbol over the tree keeps the guard working.
        root = self.build(
            {
                "composer/profiles/character_traits.h": (
                    "struct CharacterProfile { std::int8_t density_bias; };"
                ),
                "composer/form_fugue.cpp": "int t = profile.density_bias;",
            }
        )
        report = rch.check_struct_fields(root)
        self.assertTrue(report["ok"])
        self.assertEqual(report["declared_in"], ["composer/profiles/character_traits.h"])


class CaseBodyScannerTest(unittest.TestCase):
    def test_fall_through_label_has_an_empty_body(self) -> None:
        code = """
          switch (character) {
            case SubjectCharacter::Playful:
            case SubjectCharacter::Restless:
              return 56;
          }
        """
        bodies = rch.case_bodies(code, "SubjectCharacter")
        self.assertEqual(bodies["Playful"], [""])
        self.assertEqual(bodies["Restless"], ["return 56;"])

    def test_braced_body_is_captured_whole(self) -> None:
        code = """
          switch (form) {
            case FormType::Fugue: {
              build(resolved);
              break;
            }
            case FormType::Chaconne:
              other(resolved);
              break;
          }
        """
        bodies = rch.case_bodies(code, "FormType")
        self.assertEqual(bodies["Fugue"], ["{ build(resolved); break; }"])
        self.assertEqual(bodies["Chaconne"], ["other(resolved); break;"])


class EnumGuardNegativeTest(SyntheticTreeCase):
    """An enum value with no distinct outcome must be named by the guard."""

    def test_absent_value_is_reported(self) -> None:
        root = self.build(
            {"core/basic_types.h": CHARACTER_ENUM_H, "composer/form_director.cpp": DIRECTOR_CPP}
        )
        report = rch.check_enum_values("SubjectCharacter", root)
        self.assertFalse(report["ok"])
        self.assertEqual(report["unreachable"], ["Restless"])
        self.assertEqual(report["sites"]["Restless"], "absent")
        self.assertEqual(report["sites"]["Playful"], "rejected")
        self.assertEqual(report["sites"]["Severe"], "dispatched")
        self.assertEqual(report["rejection_predicates"], ["isFormCharacterCompatible"])

    def test_duplicated_outcome_is_reported(self) -> None:
        # Restless gets a case label, but its body is byte-identical to Severe's,
        # so neither value changes the output and both are reported.
        director = DIRECTOR_CPP.replace(
            "    case SubjectCharacter::Noble:\n      return 24;\n",
            "    case SubjectCharacter::Noble:\n      return 24;\n"
            "    case SubjectCharacter::Restless:\n      return 40;\n",
        )
        root = self.build(
            {"core/basic_types.h": CHARACTER_ENUM_H, "composer/form_director.cpp": director}
        )
        report = rch.check_enum_values("SubjectCharacter", root)
        self.assertFalse(report["ok"])
        self.assertEqual(sorted(report["unreachable"]), ["Restless", "Severe"])
        self.assertEqual(report["sites"]["Restless"], "shared_outcome")

    def test_fall_through_only_value_is_reported(self) -> None:
        director = DIRECTOR_CPP.replace(
            "    case SubjectCharacter::Severe:\n",
            "    case SubjectCharacter::Restless:\n    case SubjectCharacter::Severe:\n",
        )
        root = self.build(
            {"core/basic_types.h": CHARACTER_ENUM_H, "composer/form_director.cpp": director}
        )
        report = rch.check_enum_values("SubjectCharacter", root)
        self.assertEqual(report["unreachable"], ["Restless"])
        self.assertEqual(report["sites"]["Restless"], "shared_outcome")

    def test_every_value_reached_passes(self) -> None:
        director = DIRECTOR_CPP.replace(
            "    case SubjectCharacter::Noble:\n      return 24;\n",
            "    case SubjectCharacter::Noble:\n      return 24;\n"
            "    case SubjectCharacter::Restless:\n      return 72;\n",
        )
        root = self.build(
            {"core/basic_types.h": CHARACTER_ENUM_H, "composer/form_director.cpp": director}
        )
        report = rch.check_enum_values("SubjectCharacter", root)
        self.assertTrue(report["ok"], report["sites"])

    def test_caller_switch_does_not_stand_in_for_the_director(self) -> None:
        # A service that only calls buildFormFixture is not the director; its own
        # per-character switch must not make an unreached value look reached.
        caller = """
          namespace bach::application {
          Tempo tempoFor(SubjectCharacter character) {
            switch (character) {
              case SubjectCharacter::Restless:
                return 120;
            }
            return 90;
          }
          Status compose(const ComposeRequest& req) {
            if (!composer::isFormCharacterCompatible(req.form, req.character)) {
              return Status::Refused;
            }
            return composer::buildFormFixture(req, &fixture);
          }
          }  // namespace bach::application
        """
        root = self.build(
            {
                "core/basic_types.h": CHARACTER_ENUM_H,
                "composer/form_director.cpp": DIRECTOR_CPP,
                "application/composition_service.cpp": caller,
            }
        )
        report = rch.check_enum_values("SubjectCharacter", root)
        self.assertEqual(report["director_unit"], ["composer/form_director.cpp"])
        self.assertEqual(report["unreachable"], ["Restless"])


class EnumGuardLoudFailureTest(SyntheticTreeCase):
    def test_missing_enum_raises(self) -> None:
        root = self.build({"composer/form_director.cpp": DIRECTOR_CPP})
        with self.assertRaises(rch.ReachabilityError) as ctx:
            rch.check_enum_values("SubjectCharacter", root)
        self.assertIn("SubjectCharacter", str(ctx.exception))

    def test_missing_director_unit_raises(self) -> None:
        root = self.build({"core/basic_types.h": CHARACTER_ENUM_H})
        with self.assertRaises(rch.ReachabilityError) as ctx:
            rch.check_enum_values("SubjectCharacter", root)
        self.assertIn("buildFormFixture", str(ctx.exception))

    def test_director_that_never_names_the_enum_raises(self) -> None:
        director = """
          FormDirectorStatus buildFormFixture(const ComposeRequest& req, HarnessFixture* out) {
            return FormDirectorStatus::Ok;
          }
        """
        root = self.build(
            {"core/basic_types.h": CHARACTER_ENUM_H, "composer/form_director.cpp": director}
        )
        with self.assertRaises(rch.ReachabilityError) as ctx:
            rch.check_enum_values("SubjectCharacter", root)
        self.assertIn("never names", str(ctx.exception))

    def test_director_found_after_a_file_split(self) -> None:
        # The dispatch moved into a differently named unit; locating it by the
        # symbols it defines keeps the guard working.
        root = self.build(
            {
                "core/basic_types.h": CHARACTER_ENUM_H,
                "composer/director/dispatch.cpp": DIRECTOR_CPP,
            }
        )
        report = rch.check_enum_values("SubjectCharacter", root)
        self.assertEqual(report["director_unit"], ["composer/director/dispatch.cpp"])


class LiveSourceReachabilityTest(unittest.TestCase):
    """The guards against the real tree: nothing declared is currently unreached."""

    def test_character_profile_fields_have_product_readers(self) -> None:
        report = rch.check_struct_fields()
        self.assertIn("density_bias", report["fields"])
        self.assertIn("ornament_density", report["fields"])
        self.assertTrue(
            report["ok"],
            f"CharacterProfile fields with no product reader: {report['unread']}",
        )
        for field, readers in report["readers"].items():
            self.assertTrue(readers, field)

    def test_subject_characters_reach_a_distinct_outcome(self) -> None:
        report = rch.check_enum_values("SubjectCharacter")
        self.assertEqual(report["values"], ["Severe", "Playful", "Noble", "Restless"])
        self.assertTrue(
            report["ok"],
            f"SubjectCharacter values with no distinct outcome: {report['unreachable']}",
        )

    def test_form_types_reach_a_distinct_outcome(self) -> None:
        report = rch.check_enum_values("FormType")
        self.assertEqual(len(report["values"]), 10)
        self.assertTrue(
            report["ok"],
            f"FormType values with no distinct outcome: {report['unreachable']}",
        )

    def test_report_covers_both_guards(self) -> None:
        report = rch.build_report()
        guarded = [entry["enum"] for entry in report["enum_values"]]
        self.assertEqual(guarded, list(rch.GUARDED_ENUMS))
        self.assertTrue(report["ok"])


if __name__ == "__main__":
    unittest.main()
