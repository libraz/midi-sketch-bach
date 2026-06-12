# scripts/ — Python tooling

All Python tooling is driven through a single entry point:

```bash
python3 scripts/bach_tools.py --help          # full command catalog
python3 scripts/bach_tools.py <command> --help
```

Implementation lives in the `bachlib/` package (one module per concern).
`validate_generated_json.py` is a path-stable wrapper kept for the C++
`schema_validation_test` subprocess contract. The two `.mjs` files are Node
build helpers wired into `package.json` and are unrelated to `bachlib`.

External corpora (`bach-mcp`, `algomus-data`) are expected as sibling
checkouts of this repository (`../bach-mcp`, `../algomus-data`); every command
that reads them also takes an explicit path flag.

## Commands

| Command | Purpose | Inputs | Outputs | Consumed by |
|---|---|---|---|---|
| `closure` | Phase closure harness: drives `bach_cli --composer-phase <PhaseN>` across seeds, scores via bach-mcp, checks model threshold + required rule bits + byte-stable structural layout. Primary gate for composer changes. `--jobs N` parallelizes seeds (deterministic report). | built `bach_cli`, `../bach-mcp/dist/index.js` | `build/closure_report_<tag>.json` + compact JSON summary on stdout | humans / CI gating |
| `listening` | Build a listening packet: generate 20 seeds, score, render top-N to WAV. | `bach_cli`, bach-mcp | `build/listening_packet/` (manifest.json, WAVs, JSON) | human audition |
| `render` | Render one generated.json to a WAV (simple organ-like synth, no SoundFont). | generated.json | WAV file | human audition |
| `texture-gate` | Sweep seed × form and check fugue texture gates (active voices, silence, repeated runs, compass, entry-plan nonperiodicity, model score: v1 cross-entropy floor + per-form v2 KL floor). | `bach_cli`, bach-mcp | JSON to stdout (`--out` optional) | humans / CI gating |
| `completion` | Fugue completion diagnostic report (texture gates, entry plan, voice occupancy, subject rhythm, intent spans). | `bach_cli` | markdown to stdout (`--out` md file, `--json-out` JSON) | humans |
| `coverage` | Bach-technique coverage report; drift-guards `technique_catalog.json` evidence tokens against live C++ enums/rules. | `technique_catalog.json`, `src/composer/{provenance.h,voice_intent.h,validator.cpp}` | text/JSON to stdout (`--json`, `--out`) | humans / drift guard |
| `validate` | generated.v1 schema validation (no external jsonschema dep). Exit 0 pass / 1 mismatch / 2 IO error. | generated.json, `../bach-mcp/schema/generated.v1.json` | exit code + messages | C++ `schema_validation_test` (via `validate_generated_json.py`) |
| `extract-subject` | WTC I fugue-subject features from Algomus labels (external clone; not vendored). | `../algomus-data`, `../bach-mcp/data/reference` | `build/algomus_wtc_subject_features.json` | human analysis |
| `extract-subject-stats` | Fugue-subject window statistics (2nd-order interval Markov, rhythm bigrams, contour archetypes, Ryden feature distributions) from Algomus labels + a monophonic-prefix heuristic over corpus fugue movements. | `../algomus-data`, `../bach-mcp/data/reference` | intermediate JSON (default `build/subject_stats.json`, not vendored) | offline subject synthesis |
| `synth-subjects` | Offline fugue-subject pool synthesis: deterministic seeded sampling from the subject-window statistics under the catalog hard constraints (16 positions, 71,72 tail, register envelope, interval whitelist, exact 4-bar rhythm), degeneracy guards, KL-shape ranking with a trigram-NLL floor, dedup against the shipped subjects. | `build/subject_stats.json`, `src/composer/minor_material.h` (read-only) | intermediate JSON (default `build/subject_pool.json`, not vendored) | subject qualification harness |
| `qualify-subjects` | Qualify pool candidates through the real product path: each candidate is patched into all subject slots of a throwaway git worktree, `bach_cli` is rebuilt incrementally, and the fugue-family forms are judged with the full texture-gate axes (strict v2 / length-invariant floors; the v1 floor tolerates a small drop against the shipped subjects' same-combo score). Resumable JSONL; `--baseline` judges the shipped subjects; `--catalog-out` renders `subject_catalog.inc` including the per-character `kSubjectClass*` index arrays. | `build/subject_pool.json`, git worktree, bach-mcp | `build/subject_qualify.jsonl`, optional `.inc` | subject catalog (runtime selection) |
| `extract-entry-plan` | Fugue entry-plan stats (entry intervals, episode lengths, stretto rate) from `.dez`/CSV annotations. | annotation files | `src/composer/tables/entry_plan_stats.inc` (checked in) + report to stdout | C++ `form_fugue.cpp` |
| `extract-texture` | Texture bands from corpus fugues or generated.json files. | `--corpus` dir or generated.json | `src/composer/tables/texture_stats.inc` (checked in) + report to stdout | C++ tables |
| `extract-melodic` | Corpus melodic probability tables (scale degree, interval Markov, Gaussian fit). | `../bach-mcp/data/reference` | `src/composer/tables/{scale_degree_0th,interval_markov,gaussian_fit}.inc` (checked in) | C++ `melodic_tables.h` |
| `review` | Stream-segregation cue audit on bach-mcp reference JSON. | reference JSON | stdout | human analysis |
| `gen-mirror` | Regenerate `scripts/bachlib/mirror.py` from the C++ harness fixtures (the single source of truth for the mirrored data arrays). `--check` exits 1 when the committed file is stale. | `src/composer/{harness_fixture.cpp,figuration.h,provenance.h}` | `scripts/bachlib/mirror.py` (checked in) | drift guard / `test_mirror_generated.py` |

## bachlib layout

| Module | Contents |
|---|---|
| `common.py` | subprocess + bach-mcp scoring helpers (`run`, `score_generated`, `model_probability`, `heuristic_score`), provenance rule-bit counting, `generate_case()`, `REPO_ROOT`/`DEFAULT_CLI`/`DEFAULT_INDEX_JS` |
| `phases.py` | `normalize_phase`, `fixture_for_seed`, `PHASE_DEFAULTS`, `PHASE_LAYOUT`, `PHASE_TAGS` |
| `mirror.py` | **Byte-stable mirror constants** of C++ `harness_fixture.cpp` (CELLO_PRELUDE–25 layouts, subjects, required-bit sets). GENERATED by `gen_mirror.py` (`bach_tools.py gen-mirror`); regenerate rather than hand-edit. The 12 `test_*_mirror.py` drift guards plus `test_mirror_generated.py` depend on the values. |
| `gen_mirror.py` | Extracts the mirrored data arrays from the C++ source and renders `mirror.py`, making the C++ fixtures the single source of truth. Registers the `gen-mirror` command. |
| `predictors.py` | Structural predictors (`expected_*_sequence`, `structural_check`) mirroring the C++ fixtures byte-for-byte |
| `closure.py` | Closure harness engine (gates, seed loop, report) |
| `texture_metrics.py` / `texture_gate.py` / `completion.py` | Texture diagnostics shared lib + fugue gates + completion report |
| `coverage.py`, `schema.py`, `audio.py`, `review.py` | coverage / generated.v1 validation / WAV + listening packet / segregation review |
| `extract_subject.py`, `extract_entry_plan.py`, `extract_texture.py`, `extract_melodic.py` | corpus statistics extraction (`.inc` table generators) |
| `subject_stats.py` | fugue-subject window statistics (interval trigram, rhythm bigrams, contour archetypes, Ryden distributions) for offline subject synthesis; outputs intermediate JSON only |
| `subject_synth.py` | offline fugue-subject pool synthesizer (constrained Markov sampling, degeneracy guards, KL-shape ranking, dedup); outputs intermediate JSON only |
| `qualify_subjects.py` | subject qualification harness (worktree patch builds, texture-gate criteria, resumable JSONL, catalog renderer) |

Tests import these modules directly (`import bachlib as rpc` for the
mirror/predictor surface).

## Tests

```bash
python3 -m unittest discover -s scripts/tests -p "test_*.py" -v
```
