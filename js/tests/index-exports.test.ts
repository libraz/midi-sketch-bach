import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, it } from 'vitest';

const srcDir = path.resolve(import.meta.dirname, '../src');
const typesSource = readFileSync(path.join(srcDir, 'types.ts'), 'utf8');
const indexSource = readFileSync(path.join(srcDir, 'index.ts'), 'utf8');

/** Names declared by `export type` / `export interface` in types.ts. */
function declaredTypeNames(): string[] {
  const names = [...typesSource.matchAll(/^export (?:type|interface) (\w+)/gm)].map((m) => m[1]);
  return [...new Set(names)].sort();
}

/** Names listed in the `export type { ... } from './types'` block of index.ts. */
function reexportedTypeNames(): string[] {
  const block = indexSource.match(/export type \{([^}]*)\} from '\.\/types';/);
  if (!block) {
    throw new Error("index.ts has no `export type { ... } from './types'` block");
  }
  return block[1]
    .split(',')
    .map((name) => name.trim())
    .filter((name) => name.length > 0)
    .sort();
}

describe('public type surface', () => {
  it('re-exports every type declared in types.ts', () => {
    expect(reexportedTypeNames()).toEqual(declaredTypeNames());
  });
});
