import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { BachGenerator, init } from '../../js/src/index';
import type { BachConfig } from '../../js/src/types';

// MIDI header magic bytes: "MThd"
const MIDI_HEADER = [0x4d, 0x54, 0x68, 0x64];

beforeAll(async () => {
  await init();
});

describe('BachGenerator - Generation', () => {
  let bach: BachGenerator | undefined;

  afterEach(() => {
    bach?.destroy();
  });

  it('should reject generation info before a successful generation', () => {
    bach = new BachGenerator();
    expect(() => bach?.getInfo()).toThrow('Call generate() first');
    expect(() => bach?.getGenerated()).toThrow('Call generate() first');
    expect(() => bach?.getProvenance()).toThrow('Call generate() first');
  });

  it('should generate with default config', () => {
    bach = new BachGenerator();
    bach.generate();
    const midi = bach.getMidi();
    expect(midi).toBeInstanceOf(Uint8Array);
    expect(midi.length).toBeGreaterThan(0);
    expect([...midi.slice(0, 4)]).toEqual(MIDI_HEADER);
  });

  it('should generate a fugue', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42 });
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
    expect([...midi.slice(0, 4)]).toEqual(MIDI_HEADER);
  });

  it('should generate with numeric form ID', () => {
    bach = new BachGenerator();
    bach.generate({ form: 0, seed: 100 }); // 0 = Fugue
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
  });

  it('should return event data', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42 });
    const events = bach.getEvents();
    expect(events.form).toBe('fugue');
    expect(events.total_ticks).toBeGreaterThan(0);
    expect(events.tracks.length).toBeGreaterThan(0);
    expect(events.tracks[0].notes.length).toBeGreaterThan(0);
  });

  it('should expose meter, tempo, and control changes in event data', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'chaconne', seed: 42 });
    const events = bach.getEvents();
    expect(events.tempos[0]).toMatchObject({ tick: 0, bpm: 100 });
    expect(events.time_signatures[0]).toMatchObject({ tick: 0, numerator: 3, denominator: 4 });
    expect(events.tracks).toHaveLength(2);
    expect(Array.isArray(events.tracks[0].control_changes)).toBe(true);
  });

  it('should return index-parallel generated and provenance documents', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42 });
    const generated = bach.getGenerated();
    const provenance = bach.getProvenance();
    expect(generated.schema_version).toBe('generated.v1');
    expect(provenance.schema_version).toBe('provenance.v1');
    expect(generated.notes.length).toBeGreaterThan(0);
    expect(provenance.notes).toHaveLength(generated.notes.length);
    expect(typeof provenance.notes[0].satisfied_rules).toBe('string');
  });

  it('should return generation info', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42, bpm: 80 });
    const info = bach.getInfo();
    expect(info.totalBars).toBeGreaterThan(0);
    expect(info.totalTicks).toBeGreaterThan(0);
    expect(info.bpm).toBe(80);
    expect(info.trackCount).toBeGreaterThan(0);
    expect(info.seedUsed).toBe(42);
  });

  it('should produce deterministic output with same seed', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 12345 });
    const midi1 = bach.getMidi();
    bach.destroy();

    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 12345 });
    const midi2 = bach.getMidi();

    expect(midi1.length).toBe(midi2.length);
    expect([...midi1]).toEqual([...midi2]);
  });

  it('should produce different output with different seeds', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 1 });
    const midi1 = bach.getMidi();
    bach.destroy();

    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 2 });
    const midi2 = bach.getMidi();

    const same = midi1.length === midi2.length && [...midi1].every((b, i) => b === midi2[i]);
    expect(same).toBe(false);
  });

  it('should generate with scale option', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42, scale: 'medium' });
    const info = bach.getInfo();
    expect(info.totalBars).toBeGreaterThan(0);
    expect(info.totalTicks).toBeGreaterThan(0);
  });

  it('should generate with numeric scale ID', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42, scale: 1 }); // 1 = Medium
    const info = bach.getInfo();
    expect(info.totalBars).toBeGreaterThan(0);
  });

  it('should generate with targetBars override', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'chaconne', seed: 42, targetBars: 128 });
    const info = bach.getInfo();
    expect(info.totalBars).toBeGreaterThan(40);
  });

  it('targetBars should override scale and resolve near the requested length', () => {
    // targetBars snaps to the form's bar granularity and clamps to the
    // supported range, but for these forms 32 lands exactly on a boundary.
    for (const form of ['fugue', 'chaconne', 'passacaglia']) {
      bach = new BachGenerator();
      // scale 'short' would normally produce far fewer bars; targetBars wins.
      bach.generate({ form, seed: 42, scale: 'short', targetBars: 32 });
      const info = bach.getInfo();
      expect(info.totalBars).toBe(32);
      bach.destroy();
    }
    bach = undefined;
  });

  it('seed 0 should resolve to a nonzero seed', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 0 });
    const info = bach.getInfo();
    expect(info.seedUsed).not.toBe(0);
  });

  it('should throw on an invalid form string', () => {
    const generator = new BachGenerator();
    expect(() => generator.generate({ form: 'not_a_real_form' })).toThrow('getDiagnostic()');
    generator.destroy();
  });

  it('should throw on out-of-range bpm', () => {
    const generator = new BachGenerator();
    expect(() => {
      generator.generate({ form: 'fugue', bpm: 300, seed: 42 });
    }).toThrow();
    expect(() => {
      generator.generate({ form: 'fugue', bpm: 20, seed: 42 });
    }).toThrow();
    generator.destroy();
  });

  it('bpm 0 should be accepted as the default', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', bpm: 0, seed: 42 });
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
  });

  it('numVoices is accepted but ignored (backward compatibility)', () => {
    bach = new BachGenerator();
    // numVoices is no longer a config field; the form decides track count.
    bach.generate({ form: 'fugue', seed: 42, numVoices: 5 } as unknown as BachConfig);
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
  });

  it('should produce longer output with full scale', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42, scale: 'short' });
    const shortInfo = bach.getInfo();
    bach.destroy();

    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42, scale: 'full' });
    const fullInfo = bach.getInfo();

    expect(fullInfo.totalTicks).toBeGreaterThan(shortInfo.totalTicks);
  });

  it('should produce longer chaconne with full scale', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'chaconne', seed: 42 }); // default = short
    const shortInfo = bach.getInfo();
    bach.destroy();

    bach = new BachGenerator();
    bach.generate({ form: 'chaconne', seed: 42, scale: 'full' });
    const fullInfo = bach.getInfo();

    expect(fullInfo.totalBars).toBeGreaterThan(shortInfo.totalBars);
  });

  it('should return valid note data', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42 });
    const events = bach.getEvents();
    const track = events.tracks[0];

    expect(track.notes.length).toBeGreaterThan(0);

    const note = track.notes[0];
    expect(note).toHaveProperty('pitch');
    expect(note).toHaveProperty('velocity');
    expect(note).toHaveProperty('start_tick');
    expect(note).toHaveProperty('duration');
    expect(note).toHaveProperty('source');

    expect(note.pitch).toBeGreaterThanOrEqual(0);
    expect(note.pitch).toBeLessThanOrEqual(127);
    expect(note.velocity).toBeGreaterThan(0);
    expect(note.velocity).toBeLessThanOrEqual(127);
    expect(note.start_tick).toBeGreaterThanOrEqual(0);
    expect(note.duration).toBeGreaterThan(0);
    expect(typeof note.source).toBe('string');
    expect((note.source ?? '').length).toBeGreaterThan(0);
  });

  it('events JSON should expose a non-empty source on every note', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42 });
    const events = bach.getEvents();
    for (const track of events.tracks) {
      for (const note of track.notes) {
        expect(typeof note.source).toBe('string');
        expect((note.source ?? '').length).toBeGreaterThan(0);
      }
    }
  });

  it('note source should be one of the events.v1 wire values', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', seed: 42 });
    const events = bach.getEvents();
    for (const track of events.tracks) {
      for (const note of track.notes) {
        expect(['material', 'compose', 'ornament']).toContain(note.source);
      }
    }
  });
});
