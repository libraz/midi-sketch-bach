import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { BachGenerator, getVersion, init } from '../../js/src/index';
import { getApi } from '../../js/src/internal';

beforeAll(async () => {
  await init();
});

describe('BachGenerator - Basic', () => {
  let bach: BachGenerator | undefined;

  afterEach(() => {
    bach?.destroy();
  });

  it('should create a valid generator', () => {
    bach = new BachGenerator();
    expect(bach).toBeDefined();
  });

  it('should destroy without error', () => {
    bach = new BachGenerator();
    bach.destroy();
    bach = undefined;
  });

  it('should throw after destroy', () => {
    const generator = new BachGenerator();
    generator.destroy();
    expect(() => {
      generator.generate();
    }).toThrow('destroyed');
  });

  it('should allow double destroy without error', () => {
    bach = new BachGenerator();
    bach.destroy();
    bach.destroy();
    bach = undefined;
  });
});

describe('getVersion', () => {
  it('should return a valid semver version string', () => {
    const version = getVersion();
    expect(version).toMatch(/^\d+\.\d+\.\d+(\+.+)?$/);
  });

  it('should return consistent version across multiple calls', () => {
    const version1 = getVersion();
    const version2 = getVersion();
    expect(version1).toBe(version2);
  });
});

describe('BachGenerator - C ABI encoding', () => {
  it('preserves non-ASCII JSON through the C ABI', () => {
    const api = getApi();
    const handle = api.create();
    const json = '{"form":"é","seed":42}';

    try {
      // JS string length truncates the two-byte UTF-8 character, so the C
      // parser sees malformed JSON rather than a generic configuration error.
      expect(api.generateFromJson(handle, json, json.length)).toBe(9);
      expect(api.generateFromJson(handle, json, new TextEncoder().encode(json).byteLength)).toBe(3);
    } finally {
      api.destroy(handle);
    }
  });
});
