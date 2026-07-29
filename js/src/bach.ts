/**
 * BachGenerator - Main class for generating Bach MIDI compositions
 */

import { getApi, getModule } from './internal';
import type {
  BachConfig,
  BachInfo,
  DiagnosticData,
  EventData,
  GeneratedData,
  ProvenanceData,
} from './types';

/** Serialize BachConfig to JSON string for the C API. */
function configToJson(config: BachConfig): string {
  const obj: Record<string, unknown> = {};

  if (config.form !== undefined) {
    obj.form = config.form;
  }
  if (config.key !== undefined) {
    obj.key = config.key;
  }
  if (config.isMinor !== undefined) {
    obj.is_minor = config.isMinor;
  }
  if (config.bpm !== undefined) {
    obj.bpm = config.bpm;
  }
  if (config.seed !== undefined) {
    obj.seed = config.seed;
  }
  if (config.character !== undefined) {
    obj.character = config.character;
  }
  if (config.instrument !== undefined) {
    obj.instrument = config.instrument;
  }
  if (config.scale !== undefined) {
    obj.scale = config.scale;
  }
  if (config.targetBars !== undefined) {
    obj.target_bars = config.targetBars;
  }

  return JSON.stringify(obj);
}

/**
 * Bach MIDI Generator
 *
 * Creates and manages a WASM-backed Bach composition generator.
 * Must call init() before constructing.
 */
export class BachGenerator {
  private handle: number;
  private destroyed = false;

  constructor() {
    const api = getApi();
    this.handle = api.create();
  }

  /**
   * Generate a Bach composition from config.
   * @param config Generation configuration
   * @throws Error on generation failure
   */
  generate(config: BachConfig = {}): void {
    this.checkDestroyed();
    const api = getApi();
    const json = configToJson(config);
    // The C ABI expects a UTF-8 byte count, whereas String#length counts
    // UTF-16 code units. Passing the latter truncates non-ASCII JSON.
    const error = api.generateFromJson(
      this.handle,
      json,
      new TextEncoder().encode(json).byteLength,
    );
    if (error !== 0) {
      throw new Error(
        `Generation failed: ${api.errorString(error)}. ` +
          'Call getDiagnostic() for validation details when available.',
      );
    }
  }

  /**
   * Get generated MIDI data as Uint8Array.
   * @returns MIDI binary data
   * @throws Error if no generation has been done
   */
  getMidi(): Uint8Array {
    this.checkDestroyed();
    const api = getApi();
    const m = getModule();

    const ptr = api.getMidi(this.handle);
    if (ptr === 0) {
      throw new Error('No MIDI data available. Call generate() first.');
    }

    // Read BachMidiData struct: { uint8_t* data, size_t size }
    // In WASM, pointers and size_t are 4 bytes each
    const dataPtr = m.HEAPU32[ptr >> 2];
    const size = m.HEAPU32[(ptr >> 2) + 1];

    // Copy data out of WASM memory
    const result = new Uint8Array(size);
    result.set(m.HEAPU8.subarray(dataPtr, dataPtr + size));

    api.freeMidi(ptr);
    return result;
  }

  /**
   * Get event data as parsed JSON.
   * @returns Parsed event data
   * @throws Error if no generation has been done
   */
  getEvents(): EventData {
    this.checkDestroyed();
    const api = getApi();
    const m = getModule();

    const ptr = api.getEvents(this.handle);
    if (ptr === 0) {
      throw new Error('No event data available. Call generate() first.');
    }

    // Read BachEventData struct: { char* json, size_t length }
    const jsonPtr = m.HEAPU32[ptr >> 2];
    const jsonStr = m.UTF8ToString(jsonPtr);

    api.freeEvents(ptr);
    return JSON.parse(jsonStr) as EventData;
  }

  /** Get generated.v1 data from the most recent successful generation. */
  getGenerated(): GeneratedData {
    return this.getSuccessfulJson(this.requireApi().getGenerated(this.handle), 'generated.v1');
  }

  /** Get provenance.v1 data from the most recent successful generation. */
  getProvenance(): ProvenanceData {
    return this.getSuccessfulJson(this.requireApi().getProvenance(this.handle), 'provenance.v1');
  }

  /** Get diagnostic.v1 from the most recent composer validation failure. */
  getDiagnostic(): DiagnosticData | null {
    this.checkDestroyed();
    const api = getApi();
    const m = getModule();
    const ptr = api.getDiagnostic(this.handle);
    if (ptr === 0) {
      return null;
    }
    const jsonPtr = m.HEAPU32[ptr >> 2];
    const jsonStr = m.UTF8ToString(jsonPtr);
    api.freeEvents(ptr);
    return JSON.parse(jsonStr) as DiagnosticData;
  }

  /**
   * Get generation info.
   * @returns BachInfo struct data
   */
  getInfo(): BachInfo {
    this.checkDestroyed();
    const api = getApi();
    const m = getModule();

    // Use the layout-independent JSON accessor: C struct padding and size_t
    // layout vary between WASM32 and native builds.
    const ptr = api.getInfoJson(this.handle);
    if (ptr === 0) {
      throw new Error('No generation info available. Call generate() first.');
    }
    const jsonPtr = m.HEAPU32[ptr >> 2];
    const json = m.UTF8ToString(jsonPtr);
    api.freeEvents(ptr);
    return JSON.parse(json) as BachInfo;
  }

  /**
   * Destroy this instance and free WASM resources.
   * Must be called when done to prevent memory leaks.
   */
  destroy(): void {
    if (!this.destroyed) {
      const api = getApi();
      api.destroy(this.handle);
      this.destroyed = true;
    }
  }

  private checkDestroyed(): void {
    if (this.destroyed) {
      throw new Error('BachGenerator has been destroyed');
    }
  }

  private requireApi() {
    this.checkDestroyed();
    return getApi();
  }

  private getSuccessfulJson<T>(ptr: number, name: string): T {
    if (ptr === 0) {
      throw new Error(`No ${name} data available. Call generate() first.`);
    }
    const m = getModule();
    const jsonPtr = m.HEAPU32[ptr >> 2];
    const jsonStr = m.UTF8ToString(jsonPtr);
    getApi().freeEvents(ptr);
    return JSON.parse(jsonStr) as T;
  }
}
