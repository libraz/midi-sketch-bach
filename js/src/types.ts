/**
 * TypeScript type definitions for Bach MIDI Generator
 */

export type FormName =
  | 'fugue'
  | 'prelude_and_fugue'
  | 'trio_sonata'
  | 'chorale_prelude'
  | 'toccata_and_fugue'
  | 'passacaglia'
  | 'fantasia_and_fugue'
  | 'cello_prelude'
  | 'chaconne'
  | 'goldberg_variations';
export type FormId = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9;
export type KeyId = 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11;
export type KeyName = 'C' | 'C#' | 'D' | 'Eb' | 'E' | 'F' | 'F#' | 'G' | 'Ab' | 'A' | 'Bb' | 'B';
export type CharacterName = 'severe' | 'playful' | 'noble' | 'restless';
export type CharacterId = 0 | 1 | 2 | 3;
export type InstrumentName = 'organ' | 'harpsichord' | 'piano' | 'violin' | 'cello' | 'guitar';
export type InstrumentId = 0 | 1 | 2 | 3 | 4 | 5;
export type DurationScaleName = 'short' | 'medium' | 'long' | 'full';
export type DurationScaleId = 0 | 1 | 2 | 3;

/** Configuration for Bach generation. */
export interface BachConfig {
  /** Form type ID (0-9) or form name string. */
  form?: FormId | FormName;
  /** Key pitch class ID or canonical key name. */
  key?: KeyId | KeyName;
  /** True for minor mode, false for major. */
  isMinor?: boolean;
  /** Tempo in BPM (40-200, 0 = default 100). */
  bpm?: number;
  /** Random seed (0 = random). */
  seed?: number;
  /** Subject character ID (0-3) or name string. */
  character?: CharacterId | CharacterName;
  /** Instrument type ID (0-5) or name string. */
  instrument?: InstrumentId | InstrumentName;
  /** Duration scale ID (0-3) or name string ("short", "medium", "long", "full"). */
  scale?: DurationScaleId | DurationScaleName;
  /** Target bar count (overrides scale when > 0). */
  targetBars?: number;
}

/** Generation info returned after successful generation. */
export interface BachInfo {
  /** Total number of bars. */
  totalBars: number;
  /** Total duration in MIDI ticks. */
  totalTicks: number;
  /** Tempo in BPM. */
  bpm: number;
  /** Number of tracks. */
  trackCount: number;
  /** Seed used for generation. */
  seedUsed: number;
}

/** A note event from the event data (JSON keys match C API snake_case). */
export interface NoteEvent {
  pitch: number;
  velocity: number;
  start_tick: number;
  duration: number;
  voice: number;
  /** Provenance tag describing how the note was generated. */
  source?: NoteSource;
}

export type NoteSource =
  | 'unknown'
  | 'fugue_subject'
  | 'subject_core'
  | 'fugue_answer'
  | 'countersubject'
  | 'episode_material'
  | 'free_counterpoint'
  | 'cantus_fixed'
  | 'ornament'
  | 'pedal_point'
  | 'arpeggio_flow'
  | 'texture_note'
  | 'ground_bass'
  | 'collision_avoid'
  | 'post_process'
  | 'chromatic_passing'
  | 'false_entry'
  | 'coda'
  | 'sequence_note'
  | 'canon_dux'
  | 'canon_comes'
  | 'canon_free_bass'
  | 'goldberg_aria'
  | 'goldberg_bass'
  | 'goldberg_figura'
  | 'goldberg_soggetto'
  | 'quodlibet_melody'
  | 'chaconne_bass'
  | 'prelude_figuration'
  | 'toccata_gesture'
  | 'toccata_figure'
  | 'grand_pause'
  | 'cadence_approach';

/** A track from the event data (JSON keys match C API snake_case). */
export interface TrackData {
  name: string;
  channel: number;
  program: number;
  note_count: number;
  control_changes: Array<{ tick: number; controller: number; value: number }>;
  notes: NoteEvent[];
}

/** Full event data from generation (JSON keys match C API snake_case). */
export interface EventData {
  form: string;
  key: string;
  bpm: number;
  seed: number;
  total_ticks: number;
  total_bars: number;
  description: string;
  tempos: Array<{ tick: number; bpm: number }>;
  time_signatures: Array<{ tick: number; numerator: number; denominator: number }>;
  tracks: TrackData[];
}

export interface DiagnosticFailure {
  kind: 'StructuralFail' | 'MusicalFail' | 'ConfigFail';
  rule_id: string;
  span_id: number | null;
}

/** Self-contained diagnostic.v1 returned after composer validation failure. */
export interface DiagnosticData {
  schema_version: 'diagnostic.v1';
  index_parallel: boolean;
  validation: {
    status: 'ok' | 'failed_span' | 'failed_seed';
    failures: DiagnosticFailure[];
  };
  notes: Array<{
    index: number;
    start_tick: number;
    duration: number;
    pitch: number;
    voice: number;
  }>;
  provenance: ProvenanceNote[];
}

/** generated.v1 document from a successful generation. */
export interface GeneratedData {
  schema_version: 'generated.v1';
  ticks_per_beat: number;
  duration_ticks: number;
  notes: Array<{
    index: number;
    start_tick: number;
    duration: number;
    pitch: number;
    voice: number;
    velocity: number;
  }>;
}

/** provenance.v1 document from a successful generation. */
export interface ProvenanceData {
  schema_version: 'provenance.v1';
  notes: ProvenanceNote[];
}

/**
 * One note's provenance record from the provenance.v1 export
 * (emitProvenanceJson in src/composer/json_export.cpp). Index-parallel with the
 * generated.v1 notes array.
 */
export interface ProvenanceNote {
  index: number;
  /** Owning span id, or null when it equals the invalid-span sentinel. */
  span_id: number | null;
  voice_intent: string;
  /** PascalCase note source: "Material" | "Compose" | "Ornament". */
  source: 'Material' | 'Compose' | 'Ornament';
  candidate_score: number;
  /**
   * Corpus-scorer audit fields. Emitted ONLY for `source === "Compose"` notes;
   * Material and Ornament notes never run the scorer, so these keys are absent
   * for them. A present `shadow_winning_pitch` that differs from the emitted
   * pitch is the "scorer disagreed with the commit" diagnostic.
   */
  shadow_score?: number;
  shadow_winning_pitch?: number;
  shadow_winning_pitch_without_markov?: number;
  /**
   * Low 64 RuleId bits (bits 0-63) as a base-10 unsigned integer string.
   * Parse with `BigInt()`; JavaScript `number` cannot preserve all RuleId bits.
   */
  satisfied_rules: string;
  /**
   * High RuleId lane (bits 64-127) as an unsigned integer. Emitted ONLY when
   * the high lane is nonzero, so notes using no high-lane bit omit the field
   * entirely (byte-identical to the pre-high-lane output). The first high-lane
   * bit is CountersubjectInvertible (bit 64).
   */
  satisfied_rules_high?: string;
  rejected_alternatives: number;
}

/** Preset info for enumerable options. */
export interface PresetInfo {
  /** Unique ID. */
  id: number;
  /** Internal name (e.g. "fugue", "organ"). */
  name: string;
  /** Display name (e.g. "Prelude and Fugue"). */
  display?: string;
}
