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
/**
 * Instrument names as enumerated by getInstruments(). 'guitar' is reserved: no
 * form accepts it. Accepted per form: organ forms 'organ', cello_prelude
 * 'cello', chaconne 'violin', goldberg_variations 'harpsichord' or 'piano'.
 */
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
  /** Instrument type ID (0-5) or name string; must be one the form accepts. */
  instrument?: InstrumentId | InstrumentName;
  /** Duration scale ID (0-3) or name string ("short", "medium", "long", "full"). */
  scale?: DurationScaleId | DurationScaleName;
  /**
   * Target bar count (overrides scale when > 0). Rounded to the form's bar grid
   * and clamped to the form's minimum and 128; read the resolved length from
   * `BachInfo.totalBars`.
   */
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

/** Per-note provenance tag as emitted in the `events` JSON's `source` field. */
export type NoteSource = 'material' | 'compose' | 'ornament';

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

/**
 * One counterpoint rule's tally for a generated piece.
 *
 * Counted before the validator decides how to route a finding, so a rule that
 * never produced a failure is still visible. Vertical matches inside a
 * verified declared doubling (for example the toccata unison gesture) are
 * excluded from every count, so such a rule may have no entry at all.
 * `total - gated - exempted` is the number of findings recorded as
 * informational, except that `countersubject_invertible` informational entries
 * carry no tally and are not part of that difference. The counts describe
 * per-finding routing only: the counterpoint budget fails a piece through a
 * separate rule-level entry in `validation.failures` (no span) and leaves these
 * counts unchanged.
 */
export interface CounterpointObservation {
  /** Stable rule token, e.g. "parallel_fifth". */
  rule_id: string;
  /**
   * `vertical` for a relation between voices sounding together (or between a
   * voice and the harmonic plan), `linear` for one voice's own melodic
   * succession. `unclassified` means the producer has no entry for the rule.
   */
  geometry: RuleGeometry;
  /** Times the rule matched outside a declared doubling, before any routing decision. */
  total: number;
  /** Individual findings routed to validation failures. */
  gated: number;
  /** Findings suppressed at Generation scope because every operand is an immutable input. */
  exempted: number;
}

/** Rule geometry as emitted in the observation and finding records. */
export type RuleGeometry = 'linear' | 'vertical' | 'unclassified';

/** A soft finding recorded without gating generation. */
export interface InformationalFinding {
  /** Owning span id, or null when the finding has no span. */
  span_id: number | null;
  rule_id: string;
  geometry: RuleGeometry;
  kind: 'StructuralFail' | 'MusicalFail' | 'ConfigFail';
}

/** Figuration-wave reactive-layer firing counts; all zero when no layer fired. */
export interface WaveVeto {
  anchor_parallel_displaced: number;
  wobble_breaker_fired: number;
  step_parallel_adjusted: number;
  step_harsh_adjusted: number;
  order_clamp_changed: number;
  window_expanded: number;
  anchor_fault_held: number;
  total: number;
}

/** Optional per-piece analysis blocks; each key is present only when non-empty. */
export interface GeneratedInfo {
  subject_features?: Array<{
    length: number;
    range_semitones: number;
    unique_pitch_classes: number;
    opening_interval: number;
    unique_intervals: number;
    max_leap: number;
  }>;
  stream_segregation?: Array<{
    span_id: number | null;
    detected_stream_count: number;
    cell_based_stream_count: number;
    cell_count: number;
    disagrees_with_cell_counterpoint: boolean;
    stream_separation_semitones: number;
    transition_note_indices: number[];
  }>;
  texture_metrics?: Array<{
    max_active_voices: number;
    avg_active_voices: number;
    mono_ratio: number;
    compass_violation_count: number;
    register_overlap_ratio: number;
    voices: Array<{
      voice: number;
      silence_ratio: number;
      max_repeated_run: number;
      min_pitch: number;
      max_pitch: number;
    }>;
  }>;
}

/** generated.v1 document from a successful generation. */
export interface GeneratedData {
  schema_version: 'generated.v1';
  ticks_per_beat: number;
  duration_ticks: number;
  tempos?: Array<{ tick: number; bpm: number }>;
  notes: Array<{
    index: number;
    start_tick: number;
    duration: number;
    pitch: number;
    voice: number;
    velocity: number;
  }>;
  /** Always present; empty when no counterpoint rule matched. */
  counterpoint_observations: CounterpointObservation[];
  /** Always present; empty when nothing was recorded. */
  informational_findings: InformationalFinding[];
  /** Always present; all counters zero when no layer fired. */
  wave_veto: WaveVeto;
  info?: GeneratedInfo;
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
   * The fixed carrier's note as authored, before any ornament pass subdivides
   * it. Present only for notes that declare one.
   */
  authored_note?: { start_tick: number; duration: number; pitch: number };
  /**
   * Low 64 RuleId bits (bits 0-63) as a base-10 unsigned integer string.
   * Parse with `BigInt()`; JavaScript `number` cannot preserve all RuleId bits.
   */
  satisfied_rules: string;
  /**
   * High RuleId lane (bits 64-127) as an unsigned integer. Emitted ONLY when
   * the high lane is nonzero, so notes using no high-lane bit omit the field
   * entirely. The first high-lane bit (bit 64) marks countersubject membership
   * only; it does not certify invertibility. The invertibility verdict is the
   * `countersubject_invertible` entry in generated.v1 `informational_findings`.
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
