import { beforeEach, describe, expect, it, vi } from 'vitest';

const bindings = vi.hoisted(() => {
  const heap = new Uint32Array(64);
  const api = {
    create: vi.fn(() => 7),
    destroy: vi.fn(),
    getDiagnostic: vi.fn(() => 0),
    getInfoJson: vi.fn(() => 0),
    freeEvents: vi.fn(),
  };
  const module = {
    HEAPU32: heap,
    UTF8ToString: vi.fn(() => ''),
  };
  return { api, heap, module };
});

vi.mock('../src/internal', () => ({
  getApi: () => bindings.api,
  getModule: () => bindings.module,
}));

import { BachGenerator } from '../src/bach';

describe('BachGenerator.getDiagnostic', () => {
  beforeEach(() => {
    bindings.heap.fill(0);
    bindings.api.getDiagnostic.mockReset().mockReturnValue(0);
    bindings.api.freeEvents.mockReset();
    bindings.module.UTF8ToString.mockReset().mockReturnValue('');
  });

  it('returns null without attempting to free an absent diagnostic', () => {
    const generator = new BachGenerator();
    expect(generator.getDiagnostic()).toBeNull();
    expect(bindings.api.freeEvents).not.toHaveBeenCalled();
  });

  it('copies, parses, and frees the diagnostic allocation', () => {
    const structPtr = 16;
    const jsonPtr = 48;
    bindings.heap[structPtr >> 2] = jsonPtr;
    bindings.api.getDiagnostic.mockReturnValue(structPtr);
    bindings.module.UTF8ToString.mockReturnValue(
      '{"schema_version":"diagnostic.v1","index_parallel":true,"validation":{"status":"failed_span","failures":[],"informational":[]},"notes":[]}',
    );

    const generator = new BachGenerator();
    expect(generator.getDiagnostic()?.schema_version).toBe('diagnostic.v1');
    expect(bindings.module.UTF8ToString).toHaveBeenCalledWith(jsonPtr);
    expect(bindings.api.freeEvents).toHaveBeenCalledOnce();
    expect(bindings.api.freeEvents).toHaveBeenCalledWith(structPtr);
  });

  it('frees the allocation before surfacing malformed diagnostic JSON', () => {
    const structPtr = 20;
    bindings.heap[structPtr >> 2] = 52;
    bindings.api.getDiagnostic.mockReturnValue(structPtr);
    bindings.module.UTF8ToString.mockReturnValue('{');

    const generator = new BachGenerator();
    expect(() => generator.getDiagnostic()).toThrow(SyntaxError);
    expect(bindings.api.freeEvents).toHaveBeenCalledWith(structPtr);
  });
});

describe('BachGenerator.getInfo', () => {
  beforeEach(() => {
    bindings.heap.fill(0);
    bindings.api.getInfoJson.mockReset().mockReturnValue(0);
    bindings.api.freeEvents.mockReset();
    bindings.module.UTF8ToString.mockReset().mockReturnValue('');
  });

  it('rejects an absent generation result', () => {
    const generator = new BachGenerator();
    expect(() => generator.getInfo()).toThrow('Call generate() first');
    expect(bindings.api.freeEvents).not.toHaveBeenCalled();
  });

  it('parses and frees the layout-independent info JSON', () => {
    const structPtr = 24;
    const jsonPtr = 56;
    bindings.heap[structPtr >> 2] = jsonPtr;
    bindings.api.getInfoJson.mockReturnValue(structPtr);
    bindings.module.UTF8ToString.mockReturnValue(
      '{"totalBars":24,"totalTicks":46080,"bpm":96,"trackCount":3,"seedUsed":42}',
    );

    const generator = new BachGenerator();
    expect(generator.getInfo()).toEqual({
      totalBars: 24,
      totalTicks: 46080,
      bpm: 96,
      trackCount: 3,
      seedUsed: 42,
    });
    expect(bindings.module.UTF8ToString).toHaveBeenCalledWith(jsonPtr);
    expect(bindings.api.freeEvents).toHaveBeenCalledWith(structPtr);
  });
});
