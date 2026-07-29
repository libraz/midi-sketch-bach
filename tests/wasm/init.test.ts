import { describe, expect, it } from 'vitest';

describe('WASM initialization', () => {
  it('shares one in-flight initialization across concurrent callers', async () => {
    // This file deliberately performs the first init in its Vitest worker.
    // Before the in-flight cache, these calls could construct separate WASM
    // heaps and leave the exported bindings attached to only one of them.
    const { BachGenerator, init } = await import('../../js/src/index');
    await expect(Promise.all([init(), init(), init()])).resolves.toEqual([
      undefined,
      undefined,
      undefined,
    ]);

    const generator = new BachGenerator();
    generator.destroy();
  });
});
