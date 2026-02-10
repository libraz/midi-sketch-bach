/**
 * BachGenerator - Main class for generating Bach MIDI compositions
 */

import { getApi, getModule } from './internal';
import type { BachConfig, BachInfo, EventData } from './types';

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
  if (config.numVoices !== undefined) {
    obj.num_voices = config.numVoices;
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
    const error = api.generateFromJson(this.handle, json, json.length);
    if (error !== 0) {
      throw new Error(`Generation failed: ${api.errorString(error)}`);
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

  /**
   * Get generation info.
   * @returns BachInfo struct data
   */
  getInfo(): BachInfo {
    this.checkDestroyed();
    const api = getApi();
    const m = getModule();

    // bach_get_info returns pointer to static BachInfo
    const ptr = api.getInfo(this.handle);

    // BachInfo layout (C struct with natural alignment):
    //   uint16_t total_bars   @ offset 0  (2 bytes)
    //   [2 bytes padding]
    //   uint32_t total_ticks  @ offset 4  (4 bytes)
    //   uint16_t bpm          @ offset 8  (2 bytes)
    //   uint8_t  track_count  @ offset 10 (1 byte)
    //   [1 byte padding]
    //   uint32_t seed_used    @ offset 12 (4 bytes)
    //   Total: 16 bytes
    const view = new DataView(m.HEAPU8.buffer, ptr, 16);
    return {
      totalBars: view.getUint16(0, true),
      totalTicks: view.getUint32(4, true),
      bpm: view.getUint16(8, true),
      trackCount: view.getUint8(10),
      seedUsed: view.getUint32(12, true),
    };
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
}
