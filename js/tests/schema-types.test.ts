import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, it } from 'vitest';

const root = path.resolve(import.meta.dirname, '../..');
const schema = JSON.parse(readFileSync(path.join(root, 'schema/generated.v1.json'), 'utf8'));
const typesSource = readFileSync(path.join(root, 'js/src/types.ts'), 'utf8');

interface Member {
  name: string;
  optional: boolean;
}

/** Top-level members of `export interface <name> { ... }` in types.ts. */
function interfaceMembers(name: string): Member[] {
  const match = typesSource.match(new RegExp(`export interface ${name} \\{`));
  if (!match || match.index === undefined) {
    throw new Error(`interface ${name} not found`);
  }
  const members: Member[] = [];
  let depth = 0;
  for (const line of typesSource.slice(match.index).split('\n').slice(1)) {
    if (depth === 0 && line.startsWith('}')) {
      break;
    }
    const member = depth === 0 ? line.match(/^ {2}(\w+)(\?)?:/) : null;
    if (member) {
      members.push({ name: member[1], optional: member[2] === '?' });
    }
    for (const ch of line) {
      if (ch === '{' || ch === '[') {
        depth++;
      }
      if (ch === '}' || ch === ']') {
        depth--;
      }
    }
  }
  return members;
}

function expectMatches(iface: string, objectSchema: { properties: object; required?: string[] }) {
  const members = interfaceMembers(iface);
  const required = new Set(objectSchema.required ?? []);
  expect(members.map((m) => m.name).sort()).toEqual(Object.keys(objectSchema.properties).sort());
  for (const member of members) {
    expect(member.optional, `${iface}.${member.name} optionality`).toBe(!required.has(member.name));
  }
}

describe('generated.v1 TypeScript declarations', () => {
  it('GeneratedData matches the root properties and required list', () => {
    expectMatches('GeneratedData', schema);
  });

  it('CounterpointObservation matches the schema', () => {
    expectMatches('CounterpointObservation', schema.properties.counterpoint_observations.items);
  });

  it('InformationalFinding matches the schema', () => {
    expectMatches('InformationalFinding', schema.properties.informational_findings.items);
  });

  it('WaveVeto matches the schema', () => {
    expectMatches('WaveVeto', schema.properties.wave_veto);
  });
});
