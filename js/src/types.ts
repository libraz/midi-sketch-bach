/**
 * TypeScript type definitions for Bach MIDI Generator
 */

/** Configuration for Bach generation. */
export interface BachConfig {
  /** Form type ID (0-8) or form name string. */
  form?: number | string;
  /** Key pitch class (0-11: C, C#, D, Eb, E, F, F#, G, Ab, A, Bb, B). */
  key?: number;
  /** True for minor mode, false for major. */
  isMinor?: boolean;
  /** Number of voices (2-5). */
  numVoices?: number;
  /** Tempo in BPM (40-200, 0 = default 100). */
  bpm?: number;
  /** Random seed (0 = random). */
  seed?: number;
  /** Subject character ID (0-3) or name string. */
  character?: number | string;
  /** Instrument type ID (0-5) or name string. */
  instrument?: number | string;
  /** Duration scale ID (0-3) or name string ("short", "medium", "long", "full"). */
  scale?: number | string;
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
}

/** A track from the event data (JSON keys match C API snake_case). */
export interface TrackData {
  name: string;
  channel: number;
  program: number;
  note_count: number;
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
  tracks: TrackData[];
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
