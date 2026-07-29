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

describe('BachGenerator - Form-decided track count', () => {
  let bach: BachGenerator | undefined;

  afterEach(() => {
    bach?.destroy();
  });

  // The form, not the config, decides how many tracks are emitted (1-3).
  it.each([
    ['cello_prelude', 1],
    ['chaconne', 2],
    ['passacaglia', 3],
    ['fugue', 3],
    ['trio_sonata', 3],
    ['prelude_and_fugue', 3],
    ['goldberg_variations', 3],
  ] as const)('%s should emit %i track(s)', (form, expected) => {
    bach = new BachGenerator();
    bach.generate({ form, seed: 42 });
    const info = bach.getInfo();
    expect(info.trackCount).toBe(expected);
  });
});

describe('BachGenerator - Variable length', () => {
  let bach: BachGenerator | undefined;
  const allForms = [
    'fugue',
    'prelude_and_fugue',
    'trio_sonata',
    'chorale_prelude',
    'toccata_and_fugue',
    'passacaglia',
    'fantasia_and_fugue',
    'cello_prelude',
    'chaconne',
    'goldberg_variations',
  ];

  afterEach(() => {
    bach?.destroy();
  });

  // Variable length is now real: 'full' must yield more bars than 'short'
  // for the same form and seed. total_bars is meter-aware (passacaglia and
  // chaconne are 3/4), so we compare bar counts, not raw tick math.
  it.each(allForms)('%s full scale yields more bars than short', (form) => {
    bach = new BachGenerator();
    bach.generate({ form, seed: 42, scale: 'short' });
    const shortBars = bach.getInfo().totalBars;
    bach.destroy();

    bach = new BachGenerator();
    bach.generate({ form, seed: 42, scale: 'full' });
    const fullBars = bach.getInfo().totalBars;

    expect(shortBars).toBeGreaterThan(0);
    expect(fullBars).toBeGreaterThan(shortBars);
  });
});

describe('BachGenerator - All forms, major and minor', () => {
  let bach: BachGenerator | undefined;
  const allForms = [
    'fugue',
    'prelude_and_fugue',
    'trio_sonata',
    'chorale_prelude',
    'toccata_and_fugue',
    'passacaglia',
    'fantasia_and_fugue',
    'cello_prelude',
    'chaconne',
    'goldberg_variations',
  ];

  afterEach(() => {
    bach?.destroy();
  });

  it.each(allForms.flatMap((form) => [[form, false] as const, [form, true] as const]))(
    'should generate %s (minor=%s)',
    (form, isMinor) => {
      bach = new BachGenerator();
      bach.generate({ form, isMinor, seed: 42 });
      const midi = bach.getMidi();
      expect(midi.length).toBeGreaterThan(0);
      expect([...midi.slice(0, 4)]).toEqual(MIDI_HEADER);
    },
  );
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

  it.each(['severe', 'playful', 'noble', 'restless'])(
    'should generate fugue with character=%s',
    (character) => {
      bach = new BachGenerator();
      bach.generate({ form: 'fugue', character, seed: 42 });
      const midi = bach.getMidi();
      expect(midi.length).toBeGreaterThan(0);
    },
  );

  it.each(['Severe', 'Playful', 'Noble', 'Restless'])(
    'should generate fugue with enumerated character=%s',
    (character) => {
      bach = new BachGenerator();
      bach.generate({ form: 'fugue', character, seed: 42 });
      const midi = bach.getMidi();
      expect(midi.length).toBeGreaterThan(0);
    },
  );

  it('should throw on invalid character-form combo', () => {
    const generator = new BachGenerator();
    // Restless character is not compatible with ChoralePrelude.
    expect(() => {
      generator.generate({ form: 'chorale_prelude', character: 'restless', seed: 42 });
    }).toThrow();
    // Playful character is also incompatible with ChoralePrelude.
    expect(() => {
      generator.generate({ form: 'chorale_prelude', character: 'playful', seed: 42 });
    }).toThrow();
    // Noble character is not compatible with Toccata and Fugue.
    expect(() => {
      generator.generate({ form: 'toccata_and_fugue', character: 'noble', seed: 42 });
    }).toThrow();
    generator.destroy();
  });
});
