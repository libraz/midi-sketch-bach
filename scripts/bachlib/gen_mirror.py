"""Generate ``scripts/bachlib/mirror.py`` from the C++ harness fixtures.

``mirror.py`` is a committed, byte-stable Python mirror of the data arrays in
``src/composer/harness_fixture.cpp`` (plus the scale-walk helpers in
``figuration.h`` and the RuleBit values in ``provenance.h``). It is committed
because consumers import it for speed, but its values must never diverge from
the C++ source.

This module makes the C++ source the single source of truth for every mirrored
*data array*: it extracts each array straight from the C++ text (using the same
builder-anchored regular expressions the drift-guard tests trust), then renders
``mirror.py`` from a template. Constants that are not flat C++ literals -- the
scale-walk helpers, the cantus-firmus embellishment predictor, the per-form
RuleBit tuples, and a handful of imperatively-constructed layout tables -- are
held in the template and cross-checked against their C++ origin during
generation, so a value drift surfaces as a generation error rather than a
silent mismatch.

Regenerate with::

    python3 scripts/bach_tools.py gen-mirror

The :mod:`bachlib.gen_mirror` round-trip is guarded by
``scripts/tests/test_mirror_generated.py``: it regenerates the file in memory
and asserts byte-equality with the committed copy, so drift is caught
automatically.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
FIXTURE_CPP = REPO_ROOT / "src" / "composer" / "harness_fixture.cpp"
FIGURATION_H = REPO_ROOT / "src" / "composer" / "figuration.h"
PROVENANCE_H = REPO_ROOT / "src" / "composer" / "provenance.h"
MIRROR_PY = Path(__file__).resolve().parent / "mirror.py"


# ---------------------------------------------------------------------------
# C++ source extraction
# ---------------------------------------------------------------------------


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _builder_body(src: str, builder: str) -> str:
    """Return the source text following the named builder function.

    Many fixtures reuse the same local identifiers (``kBarRoot`` /
    ``kBarMinor`` / ``kGroundPitch``), so flat-int parsing must be anchored on
    the owning builder body, exactly as the drift-guard tests do.
    """
    parts = src.split(builder, 1)
    if len(parts) < 2:
        raise AssertionError(f"could not locate {builder} in harness_fixture.cpp")
    return parts[1]


def _array_initialiser(scope: str, name: str, *, source_label: str) -> str:
    """Return the brace body of the ``name[...] = { ... }`` declaration.

    Anchored on the array-declaration form (``name [extent] = {``) so leading
    comment mentions of the same identifier are never captured.
    """
    match = re.search(name + r"\s*\[[^\]]*\]\s*=\s*\{([^{}]*?)\}", scope, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name} declaration in {source_label}")
    return match.group(1)


def _parse_int_array(scope: str, name: str, *, source_label: str) -> tuple[int, ...]:
    """Parse a ``name[...] = { a, b, c };`` flat integer initialiser."""
    body = _array_initialiser(scope, name, source_label=source_label)
    return tuple(int(tok) for tok in re.split(r"[,\s]+", body) if tok)


def _parse_bool_array(scope: str, name: str, *, source_label: str) -> tuple[bool, ...]:
    """Parse a ``name[...] = { true, false, ... };`` flat bool initialiser."""
    body = _array_initialiser(scope, name, source_label=source_label)
    return tuple(tok == "true" for tok in re.split(r"[,\s]+", body) if tok)


def _parse_scale_pcs(src: str, fn_name: str) -> tuple[int, ...]:
    """Recover the pitch-class set from a ``...InScale`` membership helper."""
    match = re.search(fn_name + r"\(int pc\)\s*\{(.*?)\}", src, re.S)
    if match is None:
        raise AssertionError(f"could not locate {fn_name} in figuration.h")
    pcs = re.findall(r"p\s*==\s*(\d+)", match.group(1))
    return tuple(sorted(int(p) for p in pcs))


def _parse_2d_subjects(src: str, name: str) -> tuple[tuple[int, ...], ...]:
    """Parse a ``name = {{ {..16..}, ... }};`` 2-D subject catalog."""
    match = re.search(name + r"\s*=\s*\{\{(.*?)\}\};", src, re.S)
    if match is None:
        raise AssertionError(f"could not locate {name} in figuration.h")
    rows = re.findall(r"\{([^{}]*?)\}", match.group(1))
    return tuple(
        tuple(int(tok) for tok in re.split(r"[,\s]+", row) if tok) for row in rows
    )


def _parse_ruletbit(prov: str, name: str) -> int:
    enum_body = prov.split("enum RuleBit", 1)[1].split("};", 1)[0]
    names = dict(re.findall(r"(\w+)\s*=\s*(\d+)", enum_body))
    return int(names[name])




def extract() -> dict[str, object]:
    """Extract every mirrored data array from the C++ source.

    @return Mapping from mirror identifier to its extracted Python value. Each
            value is verified against the C++ source here, so a stale array or
            a drifted RuleBit raises ``AssertionError`` during generation.
    """
    fixture = _read(FIXTURE_CPP)
    figuration = _read(FIGURATION_H)
    provenance = _read(PROVENANCE_H)
    combined = fixture + figuration

    values: dict[str, object] = {}

    # CelloPrelude: kBarPlan (bass/mid/top per bar) + kFigures.
    cello = _builder_body(fixture, "buildCelloPreludeFixture")
    barplan_block = re.search(r"kBarPlan\[kBars\]\s*=\s*\{(.*?)\};", cello, re.S)
    if barplan_block is None:
        raise AssertionError("could not locate kBarPlan in buildCelloPreludeFixture")
    barplan_rows = re.findall(r"\{([^{}]*?)\}", barplan_block.group(1))
    values["CELLO_PRELUDE_BARPLAN"] = tuple(
        tuple(int(tok) for tok in re.split(r"[,\s]+", row) if tok)[1:]  # drop root_pc.
        for row in barplan_rows
    )
    figures_block = re.search(r"kFigures\[4\]\[4\]\s*=\s*\{(.*?)\};", cello, re.S)
    if figures_block is None:
        raise AssertionError("could not locate kFigures in buildCelloPreludeFixture")
    figures_rows = re.findall(r"\{([^{}]*?)\}", figures_block.group(1))
    values["CELLO_PRELUDE_FIGURES"] = tuple(
        tuple(int(tok) for tok in re.split(r"[,\s]+", row) if tok) for row in figures_rows
    )

    # Chaconne.
    chaconne = _builder_body(fixture, "buildChaconneFixture")
    values["CHACONNE_GROUND"] = _parse_int_array(
        chaconne, "kGroundPitch", source_label="buildChaconneFixture"
    )
    values["CHACONNE_VAR_T0"] = _parse_int_array(
        chaconne, "kVarT0", source_label="buildChaconneFixture"
    )
    values["CHACONNE_CMIN_SCALE"] = _parse_scale_pcs(figuration, "chaconneInScale")
    chaconne_blocks = re.search(r"kBlocks\[4\]\s*=\s*\{(.*?)\};", chaconne, re.S)
    if chaconne_blocks is None:
        raise AssertionError("could not locate kBlocks in buildChaconneFixture")
    values["CHACONNE_BLOCK_NOTES_PER_BEAT"] = tuple(
        int(re.findall(r"\d+", row)[-1])
        for row in re.findall(r"\{([^{}]+)\}", chaconne_blocks.group(1))
    )

    # OrganPrelude.
    organ = _builder_body(fixture, "buildOrganPreludeFixture")
    values["ORGAN_PRELUDE_BAR_ROOT"] = _parse_int_array(
        organ, "kBarRoot", source_label="buildOrganPreludeFixture"
    )
    values["ORGAN_PRELUDE_BAR_MINOR"] = _parse_bool_array(
        organ, "kBarMinor", source_label="buildOrganPreludeFixture"
    )
    values["ORGAN_PRELUDE_CMAJ_SCALE"] = _parse_scale_pcs(figuration, "organPreludeInScale")

    # OrganToccata: kBarRoot[4] only.
    toccata = _builder_body(fixture, "buildOrganToccataFixture")
    values["ORGAN_TOCCATA_BAR_ROOT"] = _parse_int_array(
        toccata, "kBarRoot", source_label="buildOrganToccataFixture"
    )

    # ChoralePrelude.
    chorale = _builder_body(fixture, "buildChoralePreludeFixture")
    values["CHORALE_PRELUDE_CF_SKELETON"] = _parse_int_array(
        chorale, "kCfSkeleton", source_label="buildChoralePreludeFixture"
    )
    values["CHORALE_PRELUDE_BAR_ROOT"] = _parse_int_array(
        chorale, "kBarRoot", source_label="buildChoralePreludeFixture"
    )

    # Passacaglia.
    passacaglia = _builder_body(fixture, "buildPassacagliaFixture")
    values["PASSACAGLIA_GROUND"] = _parse_int_array(
        passacaglia, "kGroundPitch", source_label="buildPassacagliaFixture"
    )
    values["PASSACAGLIA_VAR_T0"] = _parse_int_array(
        passacaglia, "kVarT0", source_label="buildPassacagliaFixture"
    )
    values["PASSACAGLIA_BAR_ROOT"] = _parse_int_array(
        passacaglia, "kRootPc", source_label="buildPassacagliaFixture"
    )
    values["PASSACAGLIA_BAR_MINOR"] = _parse_bool_array(
        passacaglia, "kIsMinor", source_label="buildPassacagliaFixture"
    )
    passacaglia_blocks = re.search(
        r"kBlocks\[kCycles\]\s*=\s*\{(.*?)\};", passacaglia, re.S
    )
    if passacaglia_blocks is None:
        raise AssertionError("could not locate kBlocks in buildPassacagliaFixture")
    values["PASSACAGLIA_BLOCK_NPB"] = tuple(
        int(re.findall(r"\d+", row)[1])
        for row in re.findall(r"\{([^{}]+)\}", passacaglia_blocks.group(1))
    )

    # TrioSonata.
    trio = _builder_body(fixture, "buildTrioSonataFixture")
    values["TRIO_SONATA_BAR_ROOT"] = _parse_int_array(
        trio, "kBarRoot", source_label="buildTrioSonataFixture"
    )
    values["TRIO_SONATA_BAR_MINOR"] = _parse_bool_array(
        trio, "kBarMinor", source_label="buildTrioSonataFixture"
    )
    # Register bases / densities live in the appendScalarBar call sites.
    values["TRIO_SONATA_V0_BASE"] = _scalar_base(trio, "v0.notes")
    values["TRIO_SONATA_V1_BASE"] = _scalar_base(trio, "v1.notes")
    values["TRIO_SONATA_V0_NPB"] = _scalar_npb(trio, "v0.notes")
    values["TRIO_SONATA_V1_NPB"] = _scalar_npb(trio, "v1.notes")
    # The pedal base is the `40 + root_pc` literal in the V2 loop.
    pedal_base = re.search(r"const int root_midi\s*=\s*(\d+)\s*\+\s*root_pc", trio)
    if pedal_base is None:
        raise AssertionError("could not locate trio pedal base in buildTrioSonataFixture")
    values["TRIO_SONATA_PEDAL_BASE"] = int(pedal_base.group(1))

    # Fantasia.
    fantasia = _builder_body(fixture, "buildFantasiaFixture")
    values["FANTASIA_BAR_ROOT"] = _parse_int_array(
        fantasia, "kBarRoot", source_label="buildFantasiaFixture"
    )
    values["FANTASIA_BAR_MINOR"] = _parse_bool_array(
        fantasia, "kBarMinor", source_label="buildFantasiaFixture"
    )
    specs_block = re.search(r"kSpecs\[4\]\s*=\s*\{(.*?)\};", fantasia, re.S)
    if specs_block is None:
        raise AssertionError("could not locate kSpecs[4] in buildFantasiaFixture")
    spec_rows = re.findall(
        r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*FantasiaStyle::\w+\s*,"
        r"\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}",
        specs_block.group(1),
    )
    values["FANTASIA_SECTIONS"] = tuple(
        (int(first), int(last), int(density), int(base), int(kind))
        for first, last, density, base, kind in spec_rows
    )

    # KeyboardSuite.
    suite = _builder_body(fixture, "buildKeyboardSuiteFixture")
    values["KEYBOARD_SUITE_BAR_ROOT"] = _parse_int_array(
        suite, "kBarRoot", source_label="buildKeyboardSuiteFixture"
    )
    values["KEYBOARD_SUITE_BAR_MINOR"] = _parse_bool_array(
        suite, "kBarMinor", source_label="buildKeyboardSuiteFixture"
    )
    values["KEYBOARD_SUITE_GROUND"] = _parse_int_array(
        suite, "kGroundPitch", source_label="buildKeyboardSuiteFixture"
    )
    movements_block = re.search(r"kMovements\[5\]\s*=\s*\{(.*?)\};", suite, re.S)
    if movements_block is None:
        raise AssertionError("could not locate kMovements[5] in buildKeyboardSuiteFixture")
    mvt_rows = re.findall(
        r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,"
        r"\s*FantasiaStyle::\w+\s*,\s*\d+\s*\}",
        movements_block.group(1),
    )
    values["KEYBOARD_SUITE_MOVEMENTS"] = tuple(
        (int(first), int(last), int(carrier), int(kind), int(base))
        for first, last, carrier, kind, base in mvt_rows
    )

    # GoldbergVariations.
    goldberg = _builder_body(fixture, "buildGoldbergVariationsFixture")
    values["GOLDBERG_VARIATIONS_GROUND"] = _parse_int_array(
        goldberg, "kGroundPitch", source_label="buildGoldbergVariationsFixture"
    )
    values["GOLDBERG_VARIATIONS_BAR_ROOT"] = _parse_int_array(
        goldberg, "kBarRoot", source_label="buildGoldbergVariationsFixture"
    )
    values["GOLDBERG_VARIATIONS_BAR_MINOR"] = _parse_bool_array(
        goldberg, "kBarMinor", source_label="buildGoldbergVariationsFixture"
    )
    block_spec = re.search(r"kBlockSpec\[kBlocks\]\s*=\s*\{(.*?)\};", goldberg, re.S)
    if block_spec is None:
        raise AssertionError("could not locate kBlockSpec in buildGoldbergVariationsFixture")
    block_rows = re.findall(
        r"\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(true|false)\s*\}",
        block_spec.group(1),
    )
    values["GOLDBERG_VARIATIONS_BLOCK_SPEC"] = tuple(
        (int(density), int(npb), int(base), flag == "true")
        for density, npb, base, flag in block_rows
    )

    # Shared fugue subject catalogs (figuration.h).
    values["SUBJECT_PATTERNS"] = _parse_2d_subjects(figuration, "kSubjectPatterns")
    values["FUGUE_COMPLETE_SUBJECTS"] = _parse_2d_subjects(figuration, "kFugueCompleteSubjects")

    # PreludeAndFugue prelude sections / fugue entries. These are constructed
    # imperatively in C++ (not a single literal). Recover their parameters from
    # the call sites so the mirror table stays sourced from the C++ text.
    wtc = _builder_body(fixture, "buildPreludeAndFugueFixture")
    values["PRELUDE_AND_FUGUE_BAR_ROOT"] = _parse_int_array(
        wtc, "kBarRoot", source_label="buildPreludeAndFugueFixture"
    )
    values["PRELUDE_AND_FUGUE_BAR_MINOR"] = _parse_bool_array(
        wtc, "kBarMinor", source_label="buildPreludeAndFugueFixture"
    )
    values["PRELUDE_AND_FUGUE_PRELUDE_SECTIONS"] = _wtc_prelude_sections(wtc)
    values["PRELUDE_AND_FUGUE_FUGUE_ENTRIES"] = _wtc_fugue_entries(wtc)

    # RuleBit-derived constants (provenance.h is the source of truth). The
    # FugueComplete all-technique fugue exercises every fugue device bit
    # (0..AffektCurveApplied); the later bits belong to the single-form
    # fixtures. The closure invocation supplies AffektCurveApplied + 1 masks.
    values["AFFEKT_CURVE_BIT"] = _parse_ruletbit(provenance, "AffektCurveApplied")
    values["FUGUE_COMPLETE_REQUIRED_BIT_COUNT"] = values["AFFEKT_CURVE_BIT"] + 1
    values["CELLO_PRELUDE_REQUIRED_BITS"] = (
        _parse_ruletbit(provenance, "ArpeggioFlowActive"),
        _parse_ruletbit(provenance, "ImplicitVoiceTracked"),
    )
    values["CHACONNE_REQUIRED_BITS"] = (
        _parse_ruletbit(provenance, "GroundBassReplayed"),
        _parse_ruletbit(provenance, "VariationRoleApplied"),
        _parse_ruletbit(provenance, "TextureDensityShift"),
    )
    values["ORGAN_PRELUDE_REQUIRED_BITS"] = (
        _parse_ruletbit(provenance, "FigurationCommitted"),
        _parse_ruletbit(provenance, "CadenzaApplied"),
        _parse_ruletbit(provenance, "PedalPreparation"),
    )
    values["ORGAN_TOCCATA_REQUIRED_BITS"] = (
        _parse_ruletbit(provenance, "ToccataArchetypeApplied"),
        _parse_ruletbit(provenance, "SectionTransition"),
    )
    values["CHORALE_PRELUDE_REQUIRED_BITS"] = (
        _parse_ruletbit(provenance, "CantusFirmusReplayed"),
        _parse_ruletbit(provenance, "CFEmbellishmentApplied"),
    )
    values["PASSACAGLIA_REQUIRED_BITS"] = (
        _parse_ruletbit(provenance, "PassacagliaGroundReplayed"),
        _parse_ruletbit(provenance, "VariationApplied"),
        _parse_ruletbit(provenance, "ClimaxPlaced"),
    )
    values["TRIO_SONATA_REQUIRED_BITS"] = (_parse_ruletbit(provenance, "TrioVoiceIndependent"),)
    values["FANTASIA_REQUIRED_BITS"] = (_parse_ruletbit(provenance, "FantasiaSectionContrast"),)
    values["KEYBOARD_SUITE_REQUIRED_BITS"] = (
        _parse_ruletbit(provenance, "FigurationCommitted"),
        _parse_ruletbit(provenance, "FantasiaSectionContrast"),
        _parse_ruletbit(provenance, "GroundBassReplayed"),
    )
    values["PRELUDE_AND_FUGUE_REQUIRED_BITS"] = (
        _parse_ruletbit(provenance, "FigurationCommitted"),
        _parse_ruletbit(provenance, "PedalPreparation"),
    )
    values["GOLDBERG_VARIATIONS_REQUIRED_BITS"] = (
        _parse_ruletbit(provenance, "GoldbergBassReplayed"),
        _parse_ruletbit(provenance, "GoldbergVariationRealized"),
        _parse_ruletbit(provenance, "ClimaxPlaced"),
    )

    return values


def _scalar_base(scope: str, dst: str) -> int:
    """Recover the ``base + (kBarRoot...)`` scalar base for an appendScalarBar call."""
    match = re.search(
        re.escape(dst) + r",\s*bar,\s*(\d+)\s*\+\s*\(kBarRoot", scope
    )
    if match is None:
        raise AssertionError(f"could not locate appendScalarBar base for {dst}")
    return int(match.group(1))


def _scalar_npb(scope: str, dst: str) -> int:
    """Recover the notes_per_beat for an appendScalarBar call."""
    match = re.search(
        re.escape(dst) + r",\s*bar,[^;]*?/\*notes_per_beat=\*/(\d+)", scope, re.S
    )
    if match is None:
        raise AssertionError(f"could not locate appendScalarBar notes_per_beat for {dst}")
    return int(match.group(1))


def _wtc_prelude_sections(scope: str) -> tuple[tuple[int, int, int, int, int, bool], ...]:
    """Recover the WTC prelude section table from the C++ call sites.

    Each row is (voice, first_bar, last_bar_inclusive, base_midi,
    notes_per_beat, is_pedal_prep). The two V0 figuration sections and the V1
    bass support are built imperatively; their parameters are read from the
    section windows, the ``appendFigurationBar`` base/npb arguments, and the
    ``is_pedal_prep`` flags.
    """
    sections: list[tuple[int, int, int, int, int, bool]] = []
    for tag, voice in (("sec0", 0), ("sec1", 0), ("bass", 1)):
        base = re.search(
            re.escape(tag) + r"\.notes, bar, /\*base_midi=\*/(\d+), /\*notes_per_beat=\*/(\d+)\)",
            scope,
        )
        if base is None:
            raise AssertionError(f"could not locate appendFigurationBar for {tag}")
        base_midi, npb = int(base.group(1)), int(base.group(2))
        if tag == "bass":
            first_bar, last_bar = 0, 7
        else:
            window = re.search(
                re.escape(tag) + r"\.end_tick\s*=\s*bar_tick\((\w+)\)", scope
            )
            start = re.search(re.escape(tag) + r"\.start_tick\s*=\s*bar_tick\((\d+)\)", scope)
            if window is None or start is None:
                raise AssertionError(f"could not locate {tag} window")
            first_bar = int(start.group(1))
            end_tok = window.group(1)
            last_bar = (8 if end_tok == "kPreludeBars" else int(end_tok)) - 1
        is_prep = re.search(re.escape(tag) + r"\.is_pedal_prep\s*=\s*true", scope) is not None
        sections.append((voice, first_bar, last_bar, base_midi, npb, is_prep))
    return tuple(sections)


def _wtc_fugue_entries(scope: str) -> tuple[tuple[int, int, str, int], ...]:
    """Recover the WTC fugue entry table from the push_subj_span calls.

    Each row is (voice, first_bar, intent, semis). The voice/bar/intent come
    from the ``push_subj_span`` calls; the transposition comes from the matching
    ``add_subject`` window (the V1 answer is the ``subj_pat[n] - 5`` loop).
    """
    spans = re.findall(
        r"push_subj_span\((\d+),\s*(\d+),\s*\d+,\s*VoiceIntent::(\w+)\)", scope
    )
    entries: list[tuple[int, int, str, int]] = []
    for voice, first_bar, intent in spans:
        voice_i, first_i = int(voice), int(first_bar)
        if intent == "AnswerCarrier":
            semis = -5  # the V1 real answer: subj_pat[n] - 5.
        else:
            sub = re.search(
                r"add_subject\(/\*first_bar=\*/" + str(first_i) + r", /\*semis=\*/(-?\d+)\)",
                scope,
            )
            if sub is None:
                raise AssertionError(f"could not locate add_subject for bar {first_i}")
            semis = int(sub.group(1))
        entries.append((voice_i, first_i, intent, semis))
    return tuple(entries)


# ---------------------------------------------------------------------------
# Rendering
# ---------------------------------------------------------------------------


def _fmt_int_tuple_inline(values: tuple[int, ...]) -> str:
    return "(" + ", ".join(str(v) for v in values) + ")"


def _fmt_bool_tuple_inline(values: tuple[bool, ...]) -> str:
    return "(" + ", ".join("True" if v else "False" for v in values) + ")"


def _fmt_int_rows(rows: tuple[tuple[int, ...], ...], comments: tuple[str, ...]) -> str:
    out = []
    for row, comment in zip(rows, comments):
        body = "(" + ", ".join(str(v) for v in row) + "),"
        out.append(f"    {body}{comment}")
    return "\n".join(out)


def render(values: dict[str, object]) -> str:
    """Render the full ``mirror.py`` text from the extracted values."""
    v = values
    cello_barplan = _fmt_int_rows(
        v["CELLO_PRELUDE_BARPLAN"],  # type: ignore[arg-type]
        (
            "  # C: C3  G3  E4",
            "  # F: F3  C4  A4",
            "  # G: G3  D4  B4",
            "  # C: C3  E4  G4",
            "  # F: F3  C4  A4",
            "  # G: G3  D4  B4",
            "  # C: C3  E4  G4",
            "  # C: C3  G3  E4",
        ),
    )
    cello_figures = _fmt_int_rows(
        v["CELLO_PRELUDE_FIGURES"],  # type: ignore[arg-type]
        (
            "  # mid-bass-mid-top",
            "  # mid-top-mid-bass",
            "  # bass-mid-top-mid",
            "  # top-mid-bass-mid",
        ),
    )
    organ_bar_root_inner = ", ".join(str(x) for x in v["ORGAN_PRELUDE_BAR_ROOT"])  # type: ignore[misc]
    organ_bar_minor = v["ORGAN_PRELUDE_BAR_MINOR"]  # type: ignore[assignment]
    organ_minor_rows = (
        "    "
        + ", ".join("True" if x else "False" for x in organ_bar_minor[:8])
        + ",\n    "
        + ", ".join("True" if x else "False" for x in organ_bar_minor[8:])
        + ","
    )
    chorale_cf_inner = ", ".join(str(x) for x in v["CHORALE_PRELUDE_CF_SKELETON"])  # type: ignore[misc]
    chorale_root_inner = ", ".join(str(x) for x in v["CHORALE_PRELUDE_BAR_ROOT"])  # type: ignore[misc]
    fantasia_sections = "\n".join(
        f"    ({f}, {l}, {d}, {b}, {k})," for f, l, d, b, k in v["FANTASIA_SECTIONS"]  # type: ignore[misc]
    )
    suite_movements = "\n".join(
        f"    ({f}, {l}, {c}, {k}, {b})," for f, l, c, k, b in v["KEYBOARD_SUITE_MOVEMENTS"]  # type: ignore[misc]
    )
    goldberg_block = "\n".join(
        f"    ({d}, {n}, {b}, {'True' if clx else 'False'}),"
        for d, n, b, clx in v["GOLDBERG_VARIATIONS_BLOCK_SPEC"]  # type: ignore[misc]
    )
    subject_rows = "\n".join(
        "    (" + ", ".join(str(p) for p in row) + ")," for row in v["SUBJECT_PATTERNS"]  # type: ignore[misc]
    )
    fugue_complete_rows = "\n".join(
        "    (" + ", ".join(str(p) for p in row) + ")," for row in v["FUGUE_COMPLETE_SUBJECTS"]  # type: ignore[misc]
    )
    wtc_sections = "\n".join(
        f"    ({voice}, {fb}, {lb}, {bm}, {npb}, {'True' if prep else 'False'}),"
        for voice, fb, lb, bm, npb, prep in v["PRELUDE_AND_FUGUE_PRELUDE_SECTIONS"]  # type: ignore[misc]
    )
    wtc_entry_comments = (
        "    # V0 subject verbatim.",
        "   # V1 real answer (-P4).",
        " # V2 re-entry (-P8).",
        "   # V0 stretto leader (subject verbatim).",
    )
    wtc_entries = "\n".join(
        f'    ({voice}, {fb}, "{intent}", {semis}),{comment}'
        for (voice, fb, intent, semis), comment in zip(
            v["PRELUDE_AND_FUGUE_FUGUE_ENTRIES"], wtc_entry_comments  # type: ignore[misc]
        )
    )

    return _TEMPLATE.format(
        cello_barplan=cello_barplan,
        cello_figures=cello_figures,
        chaconne_ground=_fmt_int_tuple_inline(v["CHACONNE_GROUND"]),  # type: ignore[arg-type]
        chaconne_var_t0=_fmt_int_tuple_inline(v["CHACONNE_VAR_T0"]),  # type: ignore[arg-type]
        chaconne_cmin=_fmt_int_tuple_inline(v["CHACONNE_CMIN_SCALE"]),  # type: ignore[arg-type]
        chaconne_block_npb=_fmt_int_tuple_inline(v["CHACONNE_BLOCK_NOTES_PER_BEAT"]),  # type: ignore[arg-type]
        organ_bar_root_inner=organ_bar_root_inner,
        organ_minor_rows=organ_minor_rows,
        organ_cmaj=_fmt_int_tuple_inline(v["ORGAN_PRELUDE_CMAJ_SCALE"]),  # type: ignore[arg-type]
        organ_toccata_bar_root=_fmt_int_tuple_inline(v["ORGAN_TOCCATA_BAR_ROOT"]),  # type: ignore[arg-type]
        chorale_cf_inner=chorale_cf_inner,
        chorale_root_inner=chorale_root_inner,
        passacaglia_ground=_fmt_int_tuple_inline(v["PASSACAGLIA_GROUND"]),  # type: ignore[arg-type]
        passacaglia_var_t0=_fmt_int_tuple_inline(v["PASSACAGLIA_VAR_T0"]),  # type: ignore[arg-type]
        passacaglia_bar_root=_fmt_int_tuple_inline(v["PASSACAGLIA_BAR_ROOT"]),  # type: ignore[arg-type]
        passacaglia_bar_minor=_fmt_bool_tuple_inline(v["PASSACAGLIA_BAR_MINOR"]),  # type: ignore[arg-type]
        passacaglia_block_npb=_fmt_int_tuple_inline(v["PASSACAGLIA_BLOCK_NPB"]),  # type: ignore[arg-type]
        trio_bar_root=_fmt_int_tuple_inline(v["TRIO_SONATA_BAR_ROOT"]),  # type: ignore[arg-type]
        trio_bar_minor=_fmt_bool_tuple_inline(v["TRIO_SONATA_BAR_MINOR"]),  # type: ignore[arg-type]
        trio_v0_base=v["TRIO_SONATA_V0_BASE"],
        trio_v1_base=v["TRIO_SONATA_V1_BASE"],
        trio_pedal_base=v["TRIO_SONATA_PEDAL_BASE"],
        trio_v0_npb=v["TRIO_SONATA_V0_NPB"],
        trio_v1_npb=v["TRIO_SONATA_V1_NPB"],
        fantasia_bar_root=_fmt_int_tuple_inline(v["FANTASIA_BAR_ROOT"]),  # type: ignore[arg-type]
        fantasia_bar_minor=_fmt_bool_tuple_inline(v["FANTASIA_BAR_MINOR"]),  # type: ignore[arg-type]
        fantasia_sections=fantasia_sections,
        suite_bar_root=_fmt_int_tuple_inline(v["KEYBOARD_SUITE_BAR_ROOT"]),  # type: ignore[arg-type]
        suite_bar_minor=_fmt_bool_tuple_inline(v["KEYBOARD_SUITE_BAR_MINOR"]),  # type: ignore[arg-type]
        suite_ground=_fmt_int_tuple_inline(v["KEYBOARD_SUITE_GROUND"]),  # type: ignore[arg-type]
        suite_movements=suite_movements,
        wtc_bar_root=_fmt_int_tuple_inline(v["PRELUDE_AND_FUGUE_BAR_ROOT"]),  # type: ignore[arg-type]
        wtc_bar_minor=_fmt_bool_tuple_inline(v["PRELUDE_AND_FUGUE_BAR_MINOR"]),  # type: ignore[arg-type]
        wtc_sections=wtc_sections,
        wtc_entries=wtc_entries,
        goldberg_ground=_fmt_int_tuple_inline(v["GOLDBERG_VARIATIONS_GROUND"]),  # type: ignore[arg-type]
        goldberg_bar_root=_fmt_int_tuple_inline(v["GOLDBERG_VARIATIONS_BAR_ROOT"]),  # type: ignore[arg-type]
        goldberg_bar_minor=_fmt_bool_tuple_inline(v["GOLDBERG_VARIATIONS_BAR_MINOR"]),  # type: ignore[arg-type]
        goldberg_block=goldberg_block,
        subject_rows=subject_rows,
        fugue_complete_rows=fugue_complete_rows,
        fugue_complete_bit_count=v["FUGUE_COMPLETE_REQUIRED_BIT_COUNT"],
        affekt_curve_bit=v["AFFEKT_CURVE_BIT"],
        cello_bits=_fmt_int_tuple_inline(v["CELLO_PRELUDE_REQUIRED_BITS"]),  # type: ignore[arg-type]
        chaconne_bits=_fmt_int_tuple_inline(v["CHACONNE_REQUIRED_BITS"]),  # type: ignore[arg-type]
        organ_bits=_fmt_int_tuple_inline(v["ORGAN_PRELUDE_REQUIRED_BITS"]),  # type: ignore[arg-type]
        toccata_bits=_fmt_int_tuple_inline(v["ORGAN_TOCCATA_REQUIRED_BITS"]),  # type: ignore[arg-type]
        chorale_bits=_fmt_int_tuple_inline(v["CHORALE_PRELUDE_REQUIRED_BITS"]),  # type: ignore[arg-type]
        passacaglia_bits=_fmt_int_tuple_inline(v["PASSACAGLIA_REQUIRED_BITS"]),  # type: ignore[arg-type]
        trio_bits=_fmt_single_or_tuple(v["TRIO_SONATA_REQUIRED_BITS"]),  # type: ignore[arg-type]
        fantasia_bits=_fmt_single_or_tuple(v["FANTASIA_REQUIRED_BITS"]),  # type: ignore[arg-type]
        suite_bits=_fmt_int_tuple_inline(v["KEYBOARD_SUITE_REQUIRED_BITS"]),  # type: ignore[arg-type]
        wtc_bits=_fmt_int_tuple_inline(v["PRELUDE_AND_FUGUE_REQUIRED_BITS"]),  # type: ignore[arg-type]
        goldberg_bits=_fmt_int_tuple_inline(v["GOLDBERG_VARIATIONS_REQUIRED_BITS"]),  # type: ignore[arg-type]
    )


def _fmt_single_or_tuple(values: tuple[int, ...]) -> str:
    if len(values) == 1:
        return f"({values[0]},)"
    return _fmt_int_tuple_inline(values)


def write(values: dict[str, object] | None = None) -> str:
    """Render and write ``mirror.py``; return the rendered text."""
    text = render(values if values is not None else extract())
    MIRROR_PY.write_text(text, encoding="utf-8")
    return text


# ---------------------------------------------------------------------------
# CLI registration
# ---------------------------------------------------------------------------


def register(subparsers: argparse._SubParsersAction) -> None:
    parser = subparsers.add_parser(
        "gen-mirror",
        help="Regenerate scripts/bachlib/mirror.py from the C++ harness fixtures.",
        description="Extract the mirrored data arrays from "
        "src/composer/harness_fixture.cpp (and figuration.h / provenance.h) and "
        "rewrite scripts/bachlib/mirror.py so the C++ source is the single "
        "source of truth.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Do not write; exit 1 if the committed mirror.py differs from the "
        "freshly generated text.",
    )
    parser.set_defaults(func=_run)


def _run(args: argparse.Namespace) -> int:
    generated = render(extract())
    if args.check:
        current = MIRROR_PY.read_text(encoding="utf-8")
        if current != generated:
            sys.stderr.write(
                "mirror.py is stale: regenerate with "
                "`python3 scripts/bach_tools.py gen-mirror`\n"
            )
            return 1
        sys.stdout.write("mirror.py is up to date with the C++ harness fixtures.\n")
        return 0
    MIRROR_PY.write_text(generated, encoding="utf-8")
    sys.stdout.write(f"Wrote {MIRROR_PY}\n")
    return 0


# The mirror.py template. Substitution fields carry only data-array text; all
# prose / scale-walk helpers / embellishment predictor are verbatim so the
# committed file's documentation stays intact.
_TEMPLATE = '''#!/usr/bin/env python3
"""Byte-stable C++ mirror constants for the Composer closure harness.

Every PHASE* constant and helper in this module mirrors a fixture in
``src/composer/harness_fixture.cpp`` and is a load-bearing drift guard: the
mirror-test suite (``scripts/tests/test_*_mirror.py``) asserts these stay
byte-identical with the C++ source. Do NOT reformat or edit the values; the
structural predictor reports false mismatches if they drift.

This file is GENERATED from the C++ harness fixtures by
``scripts/bachlib/gen_mirror.py``. Regenerate (rather than hand-edit) after any
fixture change::

    python3 scripts/bach_tools.py gen-mirror
"""

from __future__ import annotations


# CelloPrelude arpeggio fixture mirror. Keep byte-identical with kBarPlan / kFigures
# in buildCelloPreludeFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches.
#   kBarPlan[bar] = (root_pc unused here, bass, mid, top); I IV V I IV V I I.
CELLO_PRELUDE_BARPLAN: tuple[tuple[int, int, int], ...] = (
{cello_barplan}
)
# kFigures[seed % 4]; each slot indexes {{0=bass, 1=mid, 2=top}}.
CELLO_PRELUDE_FIGURES: tuple[tuple[int, int, int, int], ...] = (
{cello_figures}
)

# Chaconne chaconne fixture mirror. Keep byte-identical with kGroundPitch /
# kVarT0 / the C-minor scale walk in buildChaconneFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. The generated-mirror drift guard asserts these match the C++ source.
#   kGroundPitch[bar in cycle]: descending tetrachord C3 Bb2 Ab2 G2.
CHACONNE_GROUND: tuple[int, ...] = {chaconne_ground}
# kVarT0[bar in cycle]: the lowest variation tone of each chord (i VII VI V
# descent), in a singable ~C4-C5 register; each bar's scalar wave starts here.
CHACONNE_VAR_T0: tuple[int, ...] = {chaconne_var_t0}
# C natural-minor scale pitch classes (chaconneInScale in the C++ fixture).
CHACONNE_CMIN_SCALE: tuple[int, ...] = {chaconne_cmin}
# Per-block notes-per-beat (kBlocks in buildChaconneFixture): Ground=quarter,
# Respond=eighth, Propel/Assert=sixteenth. Used by the variation predictor.
CHACONNE_BLOCK_NOTES_PER_BEAT: tuple[int, ...] = {chaconne_block_npb}


def chaconne_scale_up(midi: int, steps: int) -> int:
    """Walk `steps` C-natural-minor scale degrees up from `midi`.

    Mirrors chaconneScaleUp in src/composer/harness_fixture.cpp byte-for-byte.
    """
    cur = midi
    for _ in range(steps):
        for add in range(1, 13):
            if (cur + add) % 12 in CHACONNE_CMIN_SCALE:
                cur += add
                break
    return cur

# OrganPrelude organ-prelude fixture mirror. Keep byte-identical with kBarRoot /
# kBarMinor and the C-major scale walk in buildOrganPreludeFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. A drift-guard test (test_organ_prelude_mirror.py) asserts these match
# the C++ source.
#   kBarRoot[bar]: per-bar chord root pitch class (the diatonic prelude
#   progression I V vi iii IV I ii V repeated to fill 16 bars).
ORGAN_PRELUDE_BAR_ROOT: tuple[int, ...] = (
    {organ_bar_root_inner},
)
# kBarMinor[bar]: True where the bar's diatonic degree is minor-quality
# (ii / iii / vi); selects a minor third over the root for that bar's triad.
ORGAN_PRELUDE_BAR_MINOR: tuple[bool, ...] = (
{organ_minor_rows}
)
# C major scale pitch classes (organPreludeInScale in the C++ fixture).
ORGAN_PRELUDE_CMAJ_SCALE: tuple[int, ...] = {organ_cmaj}


def organPrelude_scale_up(midi: int, steps: int) -> int:
    """Walk `steps` C-major scale degrees up from `midi`.

    Mirrors organPreludeScaleUp in src/composer/harness_fixture.cpp byte-for-byte.
    """
    cur = midi
    for _ in range(steps):
        for add in range(1, 13):
            if (cur + add) % 12 in ORGAN_PRELUDE_CMAJ_SCALE:
                cur += add
                break
    return cur

# OrganToccata organ-toccata fixture mirror. Keep byte-identical with kBarRoot in
# buildOrganToccataFixture (src/composer/harness_fixture.cpp): the per-bar chord
# root pitch classes of the diatonic C-major I IV V vi progression, cycled
# over 16 bars (bar b -> root = ORGAN_TOCCATA_BAR_ROOT[b % 4]; the vi degree,
# index 3, is minor). The OrganToccata fixture reuses the OrganPrelude C-major scale
# walk (ORGAN_PRELUDE_CMAJ_SCALE / organPrelude_scale_up) for its figuration, so no
# separate scale constant is defined here. test_organ_toccata_mirror.py guards
# ORGAN_TOCCATA_BAR_ROOT against the C++ source.
ORGAN_TOCCATA_BAR_ROOT: tuple[int, ...] = {organ_toccata_bar_root}  # I IV V vi.

# ChoralePrelude organ-chorale-prelude fixture mirror. Keep byte-identical with
# kCfSkeleton / kBarRoot and the embellishment / scale walk in
# buildChoralePreludeFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches. A drift-guard test (test_chorale_prelude_mirror.py)
# asserts these match the C++ source.
#   kCfSkeleton[bar]: the fixed chorale tune, one structural tone per bar
#   (stepwise, C3-G3). Each tone is a chord tone of that bar's chord.
CHORALE_PRELUDE_CF_SKELETON: tuple[int, ...] = (
    {chorale_cf_inner},
)
#   kBarRoot[bar]: per-bar chord root pitch class (the diatonic progression
#   I V I IV I V I V I V I IV V V V I, every triad major / kBarMinor all false).
CHORALE_PRELUDE_BAR_ROOT: tuple[int, ...] = (
    {chorale_root_inner},
)


def choralePrelude_cf_embellished() -> list[tuple[int, int]]:
    """Predict the embellished cantus firmus note stream (48 notes).

    Mirrors the embellishment loop in buildChoralePreludeFixture
    (src/composer/harness_fixture.cpp) byte-for-byte: each bar is the skeleton
    tone as a half note on the downbeat, followed by two stepwise quarter-note
    passing tones walking toward the NEXT bar's skeleton tone (the last bar
    walks back to its own tone) via the C++ ``stepToward`` helper (one diatonic
    step if the adjacent semitone is in the C-major scale, else two semitones).

    @return Sorted [(start_tick, pitch)] for the embellished CF (16 bars x 3).
    """
    ticks_per_bar = 1920
    ticks_per_beat = 480
    half = ticks_per_beat * 2
    quarter = ticks_per_beat

    def step_toward(frm: int, to: int) -> int:
        if to > frm:
            return frm + (1 if (frm + 1) % 12 in ORGAN_PRELUDE_CMAJ_SCALE else 2)
        if to < frm:
            return frm - (1 if (frm - 1) % 12 in ORGAN_PRELUDE_CMAJ_SCALE else 2)
        return frm

    seq: list[tuple[int, int]] = []
    bars = len(CHORALE_PRELUDE_CF_SKELETON)
    for bar in range(bars):
        tone = CHORALE_PRELUDE_CF_SKELETON[bar]
        nxt = CHORALE_PRELUDE_CF_SKELETON[bar + 1] if bar + 1 < bars else CHORALE_PRELUDE_CF_SKELETON[bar]
        base = bar * ticks_per_bar
        seq.append((base, tone))  # downbeat skeleton tone (half note).
        p1 = step_toward(tone, nxt)
        p2 = step_toward(p1, nxt)
        seq.append((base + half, p1))
        seq.append((base + half + quarter, p2))
    seq.sort()
    return seq


# Passacaglia organ-passacaglia fixture mirror. Keep byte-identical with kGroundPitch
# / kVarT0 / kRootPc / kIsMinor and the C-minor scale walk in buildPassacagliaFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. A drift-guard test (test_passacaglia_mirror.py) asserts these match the
# C++ source. The passacaglia reuses the Chaconne C-minor scale walk
# (CHACONNE_CMIN_SCALE / chaconne_scale_up) for its variation figuration, so no
# separate scale constant is defined here.
#   kGroundPitch[bar in cycle]: the immutable 8-bar ground bass, one whole-note
#   per bar, descending then cadencing (C3 Bb2 Ab2 G2 F2 Eb2 D2 G2).
PASSACAGLIA_GROUND: tuple[int, ...] = {passacaglia_ground}
# kVarT0[bar in cycle]: the lowest variation tone of each chord (the 8-bar cycle
# i VII VI V iv III ii0 V), in a singable ~C4-C5 register; each bar's scalar wave
# starts here.
PASSACAGLIA_VAR_T0: tuple[int, ...] = {passacaglia_var_t0}
# kRootPc[bar in cycle] / kIsMinor[bar in cycle]: the 8-bar harmonic cycle's
# per-bar chord root pitch class and minor-quality flag (i VII VI V iv III ii0 V).
PASSACAGLIA_BAR_ROOT: tuple[int, ...] = {passacaglia_bar_root}
PASSACAGLIA_BAR_MINOR: tuple[bool, ...] = {passacaglia_bar_minor}
# Per-cycle notes-per-beat (kBlocks in buildPassacagliaFixture): cycle0=eighth,
# cycle1/2=sixteenth. Used by the variation predictor.
PASSACAGLIA_BLOCK_NPB: tuple[int, ...] = {passacaglia_block_npb}

# TrioSonata organ-trio-sonata fixture mirror. Keep byte-identical with kBarRoot /
# kBarMinor and the per-voice scalar-wave / pedal construction in
# buildTrioSonataFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches. A drift-guard test (test_trio_sonata_mirror.py)
# asserts these match the C++ source. The trio sonata reuses the OrganPrelude C-major
# scale walk (ORGAN_PRELUDE_CMAJ_SCALE / organPrelude_scale_up) for its V0/V1 figuration, so
# no separate scale constant is defined here.
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 16 bars (the vi degree, index 3, is minor).
TRIO_SONATA_BAR_ROOT: tuple[int, ...] = {trio_bar_root}  # I IV V vi.
TRIO_SONATA_BAR_MINOR: tuple[bool, ...] = {trio_bar_minor}
# Per-voice register base octave offset and notes-per-beat density:
#   V0 (RH / Great):  base 72 + root_pc, 4 notes/beat (sixteenths) -> 16/bar.
#   V1 (LH / Swell):  base 60 + root_pc, 2 notes/beat (eighths)    ->  8/bar.
#   V2 (Pedal):       base 40 + root_pc, 1 note /beat (quarters)   ->  4/bar.
TRIO_SONATA_V0_BASE = {trio_v0_base}
TRIO_SONATA_V1_BASE = {trio_v1_base}
TRIO_SONATA_PEDAL_BASE = {trio_pedal_base}
TRIO_SONATA_V0_NPB = {trio_v0_npb}
TRIO_SONATA_V1_NPB = {trio_v1_npb}

# Fantasia organ-fantasia fixture mirror. Keep byte-identical with kBarRoot /
# kBarMinor and the four-section construction (kSpecs) in buildFantasiaFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. A drift-guard test (test_fantasia_mirror.py) asserts these match
# the C++ source. The fantasia reuses the OrganPrelude C-major scale walk
# (ORGAN_PRELUDE_CMAJ_SCALE / organPrelude_scale_up) for its scalar-wave figuration, so no
# separate scale constant is defined here.
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 16 bars (the vi degree, index 3, is minor).
FANTASIA_BAR_ROOT: tuple[int, ...] = {fantasia_bar_root}  # I IV V vi.
FANTASIA_BAR_MINOR: tuple[bool, ...] = {fantasia_bar_minor}
# kSpecs[4]: the four contrasting sections, each 4 bars, tiling bars 0..15.
# Each tuple is (first_bar, last_bar_inclusive, density_level, base_midi, kind)
# where kind selects the note shape: 0=quarters (4/bar), 1=eighths (8/bar),
# 2=sixteenths (16/bar), 3=half-notes (2/bar).
#   A bars 0-3:   Free,    density 4,  base C3 (48), quarters.
#   B bars 4-7:   Fugal,   density 8,  base C4 (60), eighths.
#   C bars 8-11:  Toccata, density 16, base C5 (72), sixteenths.
#   D bars 12-15: Chordal, density 2,  base C4 (60), half-notes.
FANTASIA_SECTIONS: tuple[tuple[int, int, int, int, int], ...] = (
{fantasia_sections}
)
# Validator section_contrast_required margins (kMinDensityMargin /
# kMinRegisterMargin in validator.cpp): adjacent sections must differ by >= 2
# notes/bar OR >= 5 semitones of mean register, else one soft MusicalFail.
FANTASIA_MIN_DENSITY_MARGIN = 2
FANTASIA_MIN_REGISTER_MARGIN = 5

# KeyboardSuite keyboard-suite fixture mirror. Keep byte-identical with the
# kBarRoot / kBarMinor / kGroundPitch arrays and the kMovements[5] table in
# buildKeyboardSuiteFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches. A drift-guard test (test_suite_mirror.py)
# asserts these match the C++ source. The KeyboardSuite fixture is a reuse-only
# assembly: it reuses the OrganPrelude C-major scale walk (ORGAN_PRELUDE_CMAJ_SCALE /
# organPrelude_scale_up) for its scalar-wave figuration and the GroundCarrier
# machinery for its bass, so no new scale / carrier constant is defined here.
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 20 bars (the vi degree, index 3, is minor).
KEYBOARD_SUITE_BAR_ROOT: tuple[int, ...] = {suite_bar_root}  # I IV V vi.
KEYBOARD_SUITE_BAR_MINOR: tuple[bool, ...] = {suite_bar_minor}
#   kGroundPitch[bar in cycle]: the immutable 4-bar C-major ground bass, one
#   whole-note per bar, chord root an octave low (C3 F2 G2 A2), tiled 5x.
KEYBOARD_SUITE_GROUND: tuple[int, ...] = {suite_ground}
# ground_bass_period = 4 bars * kTicksPerBar (1920) = 7680.
KEYBOARD_SUITE_GROUND_PERIOD = 4 * 1920
# kMovements[5]: the five contiguous 4-bar movements tiling bars 0..19.
# Each tuple is (first_bar, last_bar_inclusive, carrier, kind, base_midi) where
# carrier 0 = FigurationCarrier, 1 = FantasiaCarrier, and kind selects the note
# shape: 0 = eighths (8/bar), 1 = sixteenths (16/bar), 2 = half-notes (2/bar).
#   1 Prelude   bars 0-3:   FigurationCarrier, sixteenths, base C5 (72) -> 64.
#   2 Allemande bars 4-7:   FantasiaCarrier,   eighths,    base C5 (72) -> 32.
#   3 Sarabande bars 8-11:  FantasiaCarrier,   half-notes, base C5 (72) ->  8.
#   4 Courante  bars 12-15: FigurationCarrier, eighths,    base E5 (76) -> 32.
#   5 Gigue     bars 16-19: FantasiaCarrier,   sixteenths, base E5 (76) -> 64.
KEYBOARD_SUITE_MOVEMENTS: tuple[tuple[int, int, int, int, int], ...] = (
{suite_movements}
)

# PreludeAndFugue WTC-pair fixture mirror. Keep byte-identical with the
# kBarRoot[4] / kBarMinor[4] arrays, the prelude section table (two V0
# FigurationCarrier sections + one V1 bass support, with is_pedal_prep on the
# SECOND V0 section) and the fugue add_subject window / transposition scheme in
# buildPreludeAndFugueFixture (src/composer/harness_fixture.cpp) or the structural
# predictor reports false mismatches. A drift-guard test (test_wtc_pair_mirror.py)
# asserts these match the C++ source. The PreludeAndFugue fixture is a reuse-only
# assembly: the prelude reuses the OrganPrelude C-major scale walk (ORGAN_PRELUDE_CMAJ_SCALE
# / organPrelude_scale_up) and FigurationCarrier; the fugue reuses the kFugueCompleteSubjects
# catalog (FUGUE_COMPLETE_SUBJECTS) plus SubjectCarrier / AnswerCarrier, so no new scale
# / carrier / subject constant is defined here.
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 24 bars (the vi degree, index 3, is minor).
PRELUDE_AND_FUGUE_BAR_ROOT: tuple[int, ...] = {wtc_bar_root}  # I IV V vi.
PRELUDE_AND_FUGUE_BAR_MINOR: tuple[bool, ...] = {wtc_bar_minor}
# Prelude section table. Each tuple is (voice, first_bar, last_bar_inclusive,
# base_midi, notes_per_beat, is_pedal_prep):
#   V0 sec0 bars 0-3: base C4 (60), 16 sixteenths/bar -> 64 notes (bit 52).
#   V0 sec1 bars 4-7: base C4 (60), 16 sixteenths/bar, is_pedal_prep -> 64 notes
#     (bits 52 + 54). The FINAL V0 prelude section is the pedal-prep link.
#   V1 bass bars 0-7: base G3 (55), 8 eighths/bar -> 64 notes (bit 52).
# The two V0 sections share voice 0 / intent FigurationCarrier, so they merge
# into ONE (0, "FigurationCarrier") group of 128 notes; the V1 bass is its own
# (1, "FigurationCarrier") group of 64 notes.
PRELUDE_AND_FUGUE_PRELUDE_SECTIONS: tuple[tuple[int, int, int, int, int, bool], ...] = (
{wtc_sections}
)
# Fugue subject window / transposition scheme. Each tuple is (voice, first_bar,
# intent, semis). The subject pattern is FUGUE_COMPLETE_SUBJECTS[subj_a] where subj_a =
# (seed // 4) % 5. The real answer (-5) is built note-by-note like the subject
# (NO tonal mapping). Note: V0 SubjectCarrier appears in TWO windows (bars 8-11
# and the bar-20 stretto leader); both merge into one (0, "SubjectCarrier") group.
PRELUDE_AND_FUGUE_FUGUE_ENTRIES: tuple[tuple[int, int, str, int], ...] = (
{wtc_entries}
)

# GoldbergVariations Goldberg-style fixture mirror. Keep byte-identical with the
# kGroundPitch[kCycleBars] / kBarRoot[kCycleBars] / kBarMinor[kCycleBars] arrays
# and the kBlockSpec[kBlocks] table in buildGoldbergVariationsFixture
# (src/composer/harness_fixture.cpp) or the structural predictor reports false
# mismatches. A drift-guard test (test_goldberg_mirror.py) asserts these match the
# C++ source. The reduced closure fixture uses dedicated GoldbergBassCarrier /
# GoldbergVariationCarrier identities plus the OrganPrelude C-major scale walk
# (ORGAN_PRELUDE_CMAJ_SCALE / organPrelude_scale_up) for its figuration.
#   kGroundPitch[bar in cycle]: the immutable 4-bar Goldberg-style bass, one
#   whole-note per bar, chord root an octave low (C2 F2 G2 A2 = roots of I IV V
#   vi), tiled 5x over the 20 bars.
GOLDBERG_VARIATIONS_GROUND: tuple[int, ...] = {goldberg_ground}
# goldberg_aria_bass_period = 4 bars * kTicksPerBar (1920) = 7680.
GOLDBERG_VARIATIONS_GROUND_PERIOD = 4 * 1920
#   kBarRoot[bar % 4] / kBarMinor[bar % 4]: the diatonic C-major I IV V vi
#   progression cycled over 20 bars (the vi degree, index 3, is minor).
GOLDBERG_VARIATIONS_BAR_ROOT: tuple[int, ...] = {goldberg_bar_root}  # I IV V vi.
GOLDBERG_VARIATIONS_BAR_MINOR: tuple[bool, ...] = {goldberg_bar_minor}
# kBlockSpec[kBlocks]: the five contiguous 4-bar variation blocks tiling bars
# 0..19. Each tuple is (density_level, notes_per_bar m, base_midi, is_climax):
#   Block 0 Aria   bars 0-3:   density 0, m=2  (two half-notes/bar), base C5 (72),
#                              NOT climax. SPECIAL layout: not a scalar wave.
#   Block 1 Var1   bars 4-7:   density 1, m=4  (quarters),  base 72, not climax.
#   Block 2 Var2   bars 8-11:  density 2, m=8  (eighths),   base 72, not climax.
#   Block 3 Var3   bars 12-15: density 2, m=8  (eighths),   base 72, not climax.
#   Block 4 Var4   bars 16-19: density 3, m=16 (sixteenths), base 72, IS climax.
GOLDBERG_VARIATIONS_BLOCK_SPEC: tuple[tuple[int, int, int, bool], ...] = (
{goldberg_block}
)



# 5 subject patterns × 16 quarter-note pitches. Mirrors kSubjectPatterns in
# src/composer/harness_fixture.cpp; keep byte-identical with the C++ catalog
# or harness assertions diverge from CLI output.
SUBJECT_PATTERNS: tuple[tuple[int, ...], ...] = (
{subject_rows}
)

# FugueComplete uses its own subject catalog (kFugueCompleteSubjects in
# src/composer/harness_fixture.cpp): slots 0/1/4 match the shared catalog,
# slots 2/3 replace the two statistically weak subjects with higher-prob
# diatonic ones. The structural predictor must mirror this exactly or it
# reports false length/pitch mismatches for seeds that select slot 2/3.
FUGUE_COMPLETE_SUBJECTS: tuple[tuple[int, ...], ...] = (
{fugue_complete_rows}
)


# The FugueComplete fixture is the all-technique case: every provenance RuleBit
# (0..{affekt_curve_bit}) must be exercised, so the closure invocation must supply all {fugue_complete_bit_count}
# --required-rule-bit masks. provenance.h RuleBit currently tops out at
# AffektCurveApplied = {affekt_curve_bit}, so max_bit + 1 == {fugue_complete_bit_count}.
FUGUE_COMPLETE_REQUIRED_BIT_COUNT = {fugue_complete_bit_count}

# CelloPrelude (Solo String Flow) stamps exactly two RuleBits on every note:
# ArpeggioFlowActive = 47 and ImplicitVoiceTracked = 48 (provenance.h). Both
# must be asserted or the bit checks are trivially bypassed.
CELLO_PRELUDE_REQUIRED_BITS: tuple[int, ...] = {cello_bits}

# Chaconne (Solo String Arch) stamps three RuleBits across its notes (provenance.h):
# GroundBassReplayed = 49 (every ground note), VariationRoleApplied = 50 (every
# variation note), and TextureDensityShift = 51 (the first note of each
# density-changing variation). All three must be asserted or the bit checks
# are trivially bypassed.
CHACONNE_REQUIRED_BITS: tuple[int, ...] = {chaconne_bits}

# OrganPrelude (Organ Prelude) stamps three RuleBits across its notes (provenance.h):
# FigurationCommitted = 52 (every figuration note), CadenzaApplied = 53 (every
# note of the is_cadenza section), and PedalPreparation = 54 (the sustained
# dominant pedal). All three must be asserted or the bit checks
# are trivially bypassed.
ORGAN_PRELUDE_REQUIRED_BITS: tuple[int, ...] = {organ_bits}

# OrganToccata (Organ Toccata) stamps two RuleBits across its notes (provenance.h):
# ToccataArchetypeApplied = 55 (every toccata-section note) and SectionTransition
# = 56 (the first note of each section flagged is_section_head). Every archetype
# has at least one section head, so bit 56 fires >= 1 time per seed (once for
# Perpetuus, twice for Dramaticus/Sectionalis, four times for Concertato); the
# per-bit "fired anywhere in the seed" check is therefore satisfied. Both must
# be asserted or the bit checks are trivially bypassed.
ORGAN_TOCCATA_REQUIRED_BITS: tuple[int, ...] = {toccata_bits}

# ChoralePrelude (Organ Chorale Prelude) stamps two RuleBits across its notes
# (provenance.h): CantusFirmusReplayed = 57 (every cantus-firmus carrier note)
# and CFEmbellishmentApplied = 58 (every cantus-firmus note when the embellished
# line is replayed; this fixture always embellishes, so bit 58 fires on all 48
# CF notes). Both must be asserted or the bit checks are trivially
# bypassed.
CHORALE_PRELUDE_REQUIRED_BITS: tuple[int, ...] = {chorale_bits}

# Passacaglia (Organ Passacaglia) stamps three RuleBits across its notes
# (provenance.h): PassacagliaGroundReplayed = 59 (every ground note),
# VariationApplied = 60 (every variation note), and ClimaxPlaced = 61 (every note
# of the is_climax variation block). All three must be asserted or the
# bit checks are trivially bypassed.
PASSACAGLIA_REQUIRED_BITS: tuple[int, ...] = {passacaglia_bits}

# TrioSonata (Organ Trio Sonata) stamps one dedicated RuleBit across all of its
# notes (provenance.h): TrioVoiceIndependent = 62, set on every TrioVoiceCarrier
# note of all three voices. It must be asserted or the bit checks
# are trivially bypassed. (The baseline ChordTone / FugueHarmonized / FugueModulating bits
# emitMaterialNote adds are not phase-specific and so are not part of the
# dedicated closure set, matching every other carrier phase.)
TRIO_SONATA_REQUIRED_BITS: tuple[int, ...] = {trio_bits}

# Fantasia (Organ Fantasia) stamps one dedicated RuleBit across all of its notes
# (provenance.h): FantasiaSectionContrast = 63 (the LAST free RuleBit), set on
# every FantasiaCarrier note of all four contrasting sections. It must be
# asserted or the bit checks are trivially bypassed. (The baseline
# ChordTone / FugueHarmonized / FugueModulating bits emitMaterialNote adds are not phase-specific
# and so are not part of the dedicated closure set, matching every other carrier
# phase.)
FANTASIA_REQUIRED_BITS: tuple[int, ...] = {fantasia_bits}

# The KeyboardSuite keyboard suite is a reuse-only assembly: it stamps three
# already-existing RuleBits across its notes (provenance.h): FigurationCommitted
# = 52 (every FigurationCarrier dance note, movements 1 & 4),
# FantasiaSectionContrast = 63 (every FantasiaCarrier dance note, movements 2/3/5),
# and GroundBassReplayed = 49 (every GroundCarrier bass note). No new RuleBit is
# introduced. All three must be asserted or the bit checks are trivially
# bypassed.
KEYBOARD_SUITE_REQUIRED_BITS: tuple[int, ...] = {suite_bits}

# The PreludeAndFugue WTC Prelude+Fugue pair is a reuse-only assembly: it stamps
# two already-existing prelude RuleBits across its prelude notes (provenance.h):
# FigurationCommitted = 52 (every FigurationCarrier prelude note, all three
# sections) and PedalPreparation = 54 (the SECOND V0 prelude section only, the
# is_pedal_prep prelude->fugue link). No new RuleBit is introduced. The fugue
# carriers (SubjectCarrier / AnswerCarrier) have NO identity bit (they carry only
# ChordTone), so the fugue half is asserted STRUCTURALLY (span intent + bar
# windows), not by a bit. Both prelude bits must be asserted or the bit checks
# are trivially bypassed.
PRELUDE_AND_FUGUE_REQUIRED_BITS: tuple[int, ...] = {wtc_bits}

# The GoldbergVariations closure fixture stamps its dedicated Goldberg bass and
# variation RuleBits plus the shared climax marker (provenance.h). All three
# must be asserted or the bit checks are trivially bypassed.
GOLDBERG_VARIATIONS_REQUIRED_BITS: tuple[int, ...] = {goldberg_bits}
'''
