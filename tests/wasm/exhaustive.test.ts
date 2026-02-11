import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { BachGenerator, init } from '../../js/src/index';
import type { EventData } from '../../js/src/types';

beforeAll(async () => {
  await init();
});

// Simple seeded RNG (mulberry32)
function createRng(seed: number): () => number {
  return () => {
    seed |= 0;
    seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function pick<T>(arr: readonly T[], rng: () => number): T {
  return arr[Math.floor(rng() * arr.length)];
}

describe('Exhaustive Parameter Tests', () => {
  let bach: BachGenerator | undefined;

  afterEach(() => {
    bach?.destroy();
  });

  describe('All forms × multiple seeds', () => {
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
    ];
    const seeds = [1, 42, 12345, 99999];

    it.each(
      allForms.flatMap((form) => seeds.map((seed) => [form, seed] as const)),
    )('form=%s seed=%i', (form, seed) => {
      bach = new BachGenerator();
      bach.generate({ form, seed });
      const midi = bach.getMidi();
      expect(midi.length).toBeGreaterThan(0);
    });
  });

  describe('BPM sweep', () => {
    it.each([40, 60, 80, 100, 120, 140, 160, 180, 200])('bpm=%i', (bpm) => {
      bach = new BachGenerator();
      bach.generate({ form: 'fugue', bpm, seed: 42 });
      const info = bach.getInfo();
      expect(info.bpm).toBe(bpm);
    });
  });

  describe('Voice count sweep', () => {
    it.each([2, 3, 4, 5])('voices=%i', (numVoices) => {
      bach = new BachGenerator();
      bach.generate({ form: 'fugue', numVoices, seed: 42 });
      const midi = bach.getMidi();
      expect(midi.length).toBeGreaterThan(0);
    });
  });

  describe('Random combinations', () => {
    const COMBINATION_COUNT = 50;
    const rng = createRng(42);

    const forms = [
      'fugue',
      'prelude_and_fugue',
      'trio_sonata',
      'chorale_prelude',
      'toccata_and_fugue',
      'passacaglia',
      'fantasia_and_fugue',
      'cello_prelude',
      'chaconne',
    ];
    // Character-form compatibility rules:
    //   Playful/Restless × chorale_prelude = forbidden
    //   Noble × toccata_and_fugue = forbidden
    const charactersByForm: Record<string, string[]> = {
      fugue: ['severe', 'playful', 'noble', 'restless'],
      prelude_and_fugue: ['severe', 'playful', 'noble', 'restless'],
      chorale_prelude: ['severe', 'noble'],
      toccata_and_fugue: ['severe', 'playful', 'restless'],
      passacaglia: ['severe', 'playful', 'noble', 'restless'],
      fantasia_and_fugue: ['severe', 'playful', 'noble', 'restless'],
    };

    const combinations = Array.from({ length: COMBINATION_COUNT }, (_, i) => {
      const form = pick(forms, rng);
      const isOrgan = !['cello_prelude', 'chaconne'].includes(form);
      const compatibleCharacters = charactersByForm[form];
      return {
        index: i,
        form,
        key: Math.floor(rng() * 12),
        isMinor: rng() > 0.5,
        bpm: 40 + Math.floor(rng() * 161), // 40-200
        seed: Math.floor(rng() * 1000000),
        ...(isOrgan && compatibleCharacters ? { character: pick(compatibleCharacters, rng) } : {}),
      };
    });

    it.each(
      combinations.map((c) => [c.index, c] as const),
    )('combination #%i should generate without crash', (_index, config) => {
      bach = new BachGenerator();
      bach.generate(config);
      const midi = bach.getMidi();
      expect(midi.length).toBeGreaterThan(0);
    });
  });

  describe('Stress test - rapid sequential generation', () => {
    it('should handle 30 rapid sequential generations', () => {
      for (let i = 0; i < 30; i++) {
        bach = new BachGenerator();
        bach.generate({ form: 'fugue', seed: 80000 + i });
        const midi = bach.getMidi();
        expect(midi.length).toBeGreaterThan(0);
        bach.destroy();
      }
      bach = undefined;
    });
  });
});

describe('Data Integrity Validation', () => {
  let bach: BachGenerator | undefined;

  afterEach(() => {
    bach?.destroy();
  });

  function validateEventData(events: EventData, _description: string): void {
    expect(events.bpm).toBeGreaterThanOrEqual(40);
    expect(events.bpm).toBeLessThanOrEqual(200);
    expect(events.total_ticks).toBeGreaterThan(0);

    expect(events.tracks.length).toBeGreaterThan(0);

    for (const track of events.tracks) {
      for (let i = 0; i < track.notes.length; i++) {
        const note = track.notes[i];

        // Pitch validation
        expect(note.pitch).toBeGreaterThanOrEqual(0);
        expect(note.pitch).toBeLessThanOrEqual(127);

        // Velocity validation
        expect(note.velocity).toBeGreaterThan(0);
        expect(note.velocity).toBeLessThanOrEqual(127);

        // Timing validation
        expect(note.start_tick).toBeGreaterThanOrEqual(0);
        expect(note.duration).toBeGreaterThan(0);

        // Underflow check
        expect(note.duration).toBeLessThan(0x80000000);
        // Sanity: reasonable max duration (~50 bars at 480 tpb)
        expect(note.duration).toBeLessThan(100000);
      }
    }
  }

  it('should produce valid data for all organ forms', () => {
    const organForms = [
      'fugue',
      'prelude_and_fugue',
      'trio_sonata',
      'chorale_prelude',
      'toccata_and_fugue',
      'passacaglia',
      'fantasia_and_fugue',
    ];

    for (const form of organForms) {
      bach = new BachGenerator();
      bach.generate({ form, seed: 42 });
      const events = bach.getEvents();
      validateEventData(events, `form=${form}`);
      bach.destroy();
    }
    bach = undefined;
  });

  it('should produce valid data for solo string forms', () => {
    for (const form of ['cello_prelude', 'chaconne']) {
      bach = new BachGenerator();
      bach.generate({ form, seed: 42 });
      const events = bach.getEvents();
      validateEventData(events, `form=${form}`);
      bach.destroy();
    }
    bach = undefined;
  });

  it('should produce valid data across 20 random configurations', () => {
    const rng = createRng(42);
    const forms = ['fugue', 'prelude_and_fugue', 'cello_prelude', 'chaconne'];

    for (let i = 0; i < 20; i++) {
      const form = pick(forms, rng);
      bach = new BachGenerator();
      bach.generate({
        form,
        seed: Math.floor(rng() * 1000000),
        key: Math.floor(rng() * 12),
        isMinor: rng() > 0.5,
        bpm: 60 + Math.floor(rng() * 121),
      });
      const events = bach.getEvents();
      validateEventData(events, `random config #${i}`);
      bach.destroy();
    }
    bach = undefined;
  }, 30000);
});
