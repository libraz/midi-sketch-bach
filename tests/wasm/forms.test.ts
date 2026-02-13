import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { BachGenerator, init } from '../../js/src/index';

// MIDI header magic bytes: "MThd"
const MIDI_HEADER = [0x4d, 0x54, 0x68, 0x64];

beforeAll(async () => {
  await init();
});

describe('BachGenerator - Organ Forms', () => {
  let bach: BachGenerator | undefined;

  afterEach(() => {
    bach?.destroy();
  });

  it.each([
    'fugue',
    'prelude_and_fugue',
    'trio_sonata',
    'chorale_prelude',
    'toccata_and_fugue',
    'passacaglia',
    'fantasia_and_fugue',
  ])('should generate %s', (form) => {
    bach = new BachGenerator();
    bach.generate({ form, seed: 42 });
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
    expect([...midi.slice(0, 4)]).toEqual(MIDI_HEADER);
  });
});

describe('BachGenerator - Solo String Forms', () => {
  let bach: BachGenerator | undefined;

  afterEach(() => {
    bach?.destroy();
  });

  it('should generate cello prelude', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'cello_prelude', seed: 42 });
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
    expect([...midi.slice(0, 4)]).toEqual(MIDI_HEADER);
  });

  it('should generate chaconne', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'chaconne', seed: 42 });
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
    expect([...midi.slice(0, 4)]).toEqual(MIDI_HEADER);
  });
});

describe('BachGenerator - Goldberg Variations', () => {
  let bach: BachGenerator | undefined;

  afterEach(() => {
    bach?.destroy();
  });

  it('should generate goldberg_variations', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'goldberg_variations', seed: 42 });
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
    expect([...midi.slice(0, 4)]).toEqual(MIDI_HEADER);
  });

  it('should generate goldberg_variations with different scales', () => {
    for (const scale of ['short', 'medium', 'long']) {
      bach = new BachGenerator();
      bach.generate({ form: 'goldberg_variations', scale, seed: 42 });
      const midi = bach.getMidi();
      expect(midi.length).toBeGreaterThan(0);
      bach.destroy();
    }
    bach = undefined;
  });

  it('should generate goldberg_variations with G major key', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'goldberg_variations', key: 7, seed: 42 });
    const events = bach.getEvents();
    expect(events.key).toContain('G');
  });

  it('should produce deterministic output', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'goldberg_variations', seed: 42 });
    const midi1 = bach.getMidi();
    bach.destroy();

    bach = new BachGenerator();
    bach.generate({ form: 'goldberg_variations', seed: 42 });
    const midi2 = bach.getMidi();

    expect(midi1.length).toBe(midi2.length);
    expect([...midi1]).toEqual([...midi2]);
  });
});

describe('BachGenerator - Key and Character', () => {
  let bach: BachGenerator | undefined;

  afterEach(() => {
    bach?.destroy();
  });

  it('should generate with key and minor mode', () => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', key: 7, isMinor: true, seed: 42 }); // G minor
    const events = bach.getEvents();
    expect(events.key).toContain('G');
    expect(events.key).toContain('minor');
  });

  it.each([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11])('should generate with key=%i', (key) => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', key, seed: 42 });
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
  });

  it.each([
    'severe',
    'playful',
    'noble',
    'restless',
  ])('should generate fugue with character=%s', (character) => {
    bach = new BachGenerator();
    bach.generate({ form: 'fugue', character, seed: 42 });
    const midi = bach.getMidi();
    expect(midi.length).toBeGreaterThan(0);
  });

  it('should throw on invalid character-form combo', () => {
    const generator = new BachGenerator();
    // Restless character is not compatible with ChoralePrelude
    expect(() => {
      generator.generate({ form: 'chorale_prelude', character: 'restless', seed: 42 });
    }).toThrow();
    generator.destroy();
  });
});
