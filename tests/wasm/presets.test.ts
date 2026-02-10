import { beforeAll, describe, expect, it } from 'vitest';
import {
  getCharacters,
  getDefaultInstrumentForForm,
  getForms,
  getInstruments,
  getKeys,
  getScales,
  init,
} from '../../js/src/index';

beforeAll(async () => {
  await init();
});

describe('Presets', () => {
  describe('getForms', () => {
    it('should return 9 forms', () => {
      const forms = getForms();
      expect(forms.length).toBe(9);
    });

    it('should return correct form names', () => {
      const forms = getForms();
      expect(forms[0].name).toBe('fugue');
      expect(forms[0].display).toBe('Fugue');
      expect(forms[1].name).toBe('prelude_and_fugue');
      expect(forms[1].display).toBe('Prelude and Fugue');
      expect(forms[8].name).toBe('chaconne');
    });
  });

  describe('getInstruments', () => {
    it('should return 6 instruments', () => {
      const instruments = getInstruments();
      expect(instruments.length).toBe(6);
    });

    it('should return correct instrument names', () => {
      const instruments = getInstruments();
      expect(instruments[0].name).toBe('organ');
      expect(instruments[3].name).toBe('violin');
      expect(instruments[4].name).toBe('cello');
      expect(instruments[5].name).toBe('guitar');
    });
  });

  describe('getCharacters', () => {
    it('should return 4 characters', () => {
      const characters = getCharacters();
      expect(characters.length).toBe(4);
    });

    it('should include all character types', () => {
      const names = getCharacters().map((c) => c.name);
      expect(names).toContain('Severe');
      expect(names).toContain('Playful');
      expect(names).toContain('Noble');
      expect(names).toContain('Restless');
    });
  });

  describe('getKeys', () => {
    it('should return 12 keys', () => {
      const keys = getKeys();
      expect(keys.length).toBe(12);
    });

    it('should return correct key names', () => {
      const keys = getKeys();
      expect(keys[0].name).toBe('C');
      expect(keys[7].name).toBe('G');
    });
  });

  describe('getScales', () => {
    it('should return 4 scales', () => {
      const scales = getScales();
      expect(scales.length).toBe(4);
    });

    it('should return correct scale names', () => {
      const scales = getScales();
      expect(scales[0].name).toBe('short');
      expect(scales[1].name).toBe('medium');
      expect(scales[2].name).toBe('long');
      expect(scales[3].name).toBe('full');
    });

    it('should have sequential IDs', () => {
      const scales = getScales();
      scales.forEach((s, i) => {
        expect(s.id).toBe(i);
      });
    });
  });

  describe('getDefaultInstrumentForForm', () => {
    it('should return organ for organ forms', () => {
      expect(getDefaultInstrumentForForm(0)).toBe(0); // fugue -> organ
      expect(getDefaultInstrumentForForm(1)).toBe(0); // prelude_and_fugue -> organ
    });

    it('should return cello for cello prelude', () => {
      expect(getDefaultInstrumentForForm(7)).toBe(4); // cello_prelude -> cello
    });

    it('should return violin for chaconne', () => {
      expect(getDefaultInstrumentForForm(8)).toBe(3); // chaconne -> violin
    });
  });
});
