/**
 * midi-sketch-bach - Bach Instrumental MIDI Generator
 *
 * @example
 * ```ts
 * import { init, BachGenerator, getForms } from '@libraz/midi-sketch-bach';
 *
 * await init();
 *
 * const bach = new BachGenerator();
 * bach.generate({ form: 'fugue', key: 7, isMinor: true, seed: 42 });
 * const midi = bach.getMidi();
 * bach.destroy();
 * ```
 */

// Main generator class
export { BachGenerator } from './bach';
// Initialization
export { init } from './internal';
// Preset enumeration
export {
  getCharacters,
  getDefaultInstrumentForForm,
  getForms,
  getInstruments,
  getKeys,
  getScales,
  getVersion,
} from './presets';
// Type definitions
export type { BachConfig, BachInfo, EventData, NoteEvent, PresetInfo, TrackData } from './types';
