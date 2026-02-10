/**
 * Preset enumeration functions for forms, instruments, characters, and keys
 */

import { getApi } from './internal';
import type { PresetInfo } from './types';

/** Get all available form types. */
export function getForms(): PresetInfo[] {
  const api = getApi();
  const count = api.formCount();
  const forms: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    forms.push({
      id: i,
      name: api.formName(i),
      display: api.formDisplay(i),
    });
  }
  return forms;
}

/** Get all available instruments. */
export function getInstruments(): PresetInfo[] {
  const api = getApi();
  const count = api.instrumentCount();
  const instruments: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    instruments.push({
      id: i,
      name: api.instrumentName(i),
    });
  }
  return instruments;
}

/** Get all available subject characters. */
export function getCharacters(): PresetInfo[] {
  const api = getApi();
  const count = api.characterCount();
  const characters: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    characters.push({
      id: i,
      name: api.characterName(i),
    });
  }
  return characters;
}

/** Get all available keys. */
export function getKeys(): PresetInfo[] {
  const api = getApi();
  const count = api.keyCount();
  const keys: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    keys.push({
      id: i,
      name: api.keyName(i),
    });
  }
  return keys;
}

/** Get all available duration scales. */
export function getScales(): PresetInfo[] {
  const api = getApi();
  const count = api.scaleCount();
  const scales: PresetInfo[] = [];
  for (let i = 0; i < count; i++) {
    scales.push({
      id: i,
      name: api.scaleName(i),
    });
  }
  return scales;
}

/** Get the default instrument ID for a given form type. */
export function getDefaultInstrumentForForm(formId: number): number {
  return getApi().defaultInstrumentForForm(formId);
}

/** Get the library version string. */
export function getVersion(): string {
  return getApi().version();
}
