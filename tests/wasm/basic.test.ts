import { afterEach, beforeAll, describe, expect, it } from 'vitest';
import { BachGenerator, getVersion, init } from '../../js/src/index';

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
