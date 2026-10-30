import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, it } from 'vitest';

const root = path.resolve(import.meta.dirname, '../..');

/** snake_case wire name from a FormType / SubjectCharacter enumerator. */
function wireName(enumerator: string): string {
  return enumerator.replace(/(?<!^)([A-Z])/g, '_$1').toLowerCase();
}

/** Pairs rejected by isFormCharacterCompatible, as "form:character". */
function nativeIncompatiblePairs(): string[] {
  const source = readFileSync(path.join(root, 'src/composer/form_director.cpp'), 'utf8');
  const start = source.indexOf('bool isFormCharacterCompatible(');
  expect(start).toBeGreaterThanOrEqual(0);
  const body = source.slice(start, source.indexOf('\n}\n', start));
  const pairs: string[] = [];
  for (const block of body.split(/if \(form == FormType::/).slice(1)) {
    const form = wireName(block.match(/^(\w+)\)/)?.[1] ?? '');
    const characters = [...block.matchAll(/character == SubjectCharacter::(\w+)/g)];
    for (const match of characters) {
      pairs.push(`${form}:${wireName(match[1])}`);
    }
  }
  return pairs.sort();
}

/** Pairs listed in the demo's INCOMPATIBLE_CHARACTERS_BY_FORM, as "form:character". */
function demoIncompatiblePairs(): string[] {
  const html = readFileSync(path.join(root, 'demo/index.html'), 'utf8');
  const start = html.indexOf('const INCOMPATIBLE_CHARACTERS_BY_FORM = {');
  expect(start).toBeGreaterThanOrEqual(0);
  const body = html.slice(start, html.indexOf('\n    };', start));
  const pairs: string[] = [];
  for (const entry of body.matchAll(/'(\w+)': new Set\(\[([^\]]*)\]\)/g)) {
    for (const character of entry[2].matchAll(/'(\w+)'/g)) {
      pairs.push(`${entry[1]}:${character[1]}`);
    }
  }
  return pairs.sort();
}

describe('demo form/character compatibility table', () => {
  it('lists exactly the pairs isFormCharacterCompatible rejects', () => {
    const native = nativeIncompatiblePairs();
    expect(native.length).toBeGreaterThan(0);
    expect(demoIncompatiblePairs()).toEqual(native);
  });
});
