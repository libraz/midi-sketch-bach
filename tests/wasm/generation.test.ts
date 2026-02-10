import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { BachGenerator, init } from '../../js/src/index';

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

    expect(note.pitch).toBeGreaterThanOrEqual(0);
    expect(note.pitch).toBeLessThanOrEqual(127);
    expect(note.velocity).toBeGreaterThan(0);
    expect(note.velocity).toBeLessThanOrEqual(127);
    expect(note.start_tick).toBeGreaterThanOrEqual(0);
    expect(note.duration).toBeGreaterThan(0);
  });
});
