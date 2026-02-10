/**
 * Internal WASM module bindings and initialization
 * @internal
 */

// ============================================================================
// Types for Emscripten Module
// ============================================================================

export interface EmscriptenModule {
  cwrap: (
    name: string,
    returnType: string | null,
    argTypes: string[],
  ) => (...args: unknown[]) => unknown;
  UTF8ToString: (ptr: number) => string;
  HEAPU8: Uint8Array;
  HEAPU32: Uint32Array;
}

export interface Api {
  // Lifecycle
  create: () => number;
  destroy: (handle: number) => void;
  // Generation
  generateFromJson: (handle: number, json: string, length: number) => number;
  // Output
  getMidi: (handle: number) => number;
  freeMidi: (ptr: number) => void;
  getEvents: (handle: number) => number;
  freeEvents: (ptr: number) => void;
  getInfo: (handle: number) => number;
  // Form enumeration
  formCount: () => number;
  formName: (id: number) => string;
  formDisplay: (id: number) => string;
  // Instrument enumeration
  instrumentCount: () => number;
  instrumentName: (id: number) => string;
  // Character enumeration
  characterCount: () => number;
  characterName: (id: number) => string;
  // Key enumeration
  keyCount: () => number;
  keyName: (id: number) => string;
  // Scale enumeration
  scaleCount: () => number;
  scaleName: (id: number) => string;
  // Default instrument
  defaultInstrumentForForm: (formId: number) => number;
  // Error handling
  errorString: (error: number) => string;
  // Version
  version: () => string;
}

// ============================================================================
// Module State
// ============================================================================

let moduleInstance: EmscriptenModule | null = null;
let api: Api | null = null;

/**
 * Get the WASM module instance
 * @throws Error if module not initialized
 * @internal
 */
export function getModule(): EmscriptenModule {
  if (!moduleInstance) {
    throw new Error('Module not initialized. Call init() first.');
  }
  return moduleInstance;
}

/**
 * Get the API bindings
 * @throws Error if module not initialized
 * @internal
 */
export function getApi(): Api {
  if (!api) {
    throw new Error('Module not initialized. Call init() first.');
  }
  return api;
}

// ============================================================================
// Initialization
// ============================================================================

/**
 * Initialize the WASM module
 */
export async function init(options?: { wasmPath?: string }): Promise<void> {
  if (moduleInstance) {
    return;
  }

  const createModule = await import('../bach.js');
  const moduleOpts: Record<string, unknown> = {};
  if (options?.wasmPath) {
    moduleOpts.locateFile = (path: string) => {
      if (path.endsWith('.wasm')) {
        return options.wasmPath;
      }
      return path;
    };
  }
  moduleInstance = await createModule.default(moduleOpts);

  if (!moduleInstance) {
    throw new Error('Failed to initialize WASM module');
  }
  const m = moduleInstance;

  api = {
    // Lifecycle
    create: m.cwrap('bach_create', 'number', []) as () => number,
    destroy: m.cwrap('bach_destroy', null, ['number']) as (handle: number) => void,
    // Generation
    generateFromJson: m.cwrap('bach_generate_from_json', 'number', [
      'number',
      'string',
      'number',
    ]) as (handle: number, json: string, length: number) => number,
    // Output
    getMidi: m.cwrap('bach_get_midi', 'number', ['number']) as (handle: number) => number,
    freeMidi: m.cwrap('bach_free_midi', null, ['number']) as (ptr: number) => void,
    getEvents: m.cwrap('bach_get_events', 'number', ['number']) as (handle: number) => number,
    freeEvents: m.cwrap('bach_free_events', null, ['number']) as (ptr: number) => void,
    getInfo: m.cwrap('bach_get_info', 'number', ['number']) as (handle: number) => number,
    // Form enumeration
    formCount: m.cwrap('bach_form_count', 'number', []) as () => number,
    formName: m.cwrap('bach_form_name', 'string', ['number']) as (id: number) => string,
    formDisplay: m.cwrap('bach_form_display', 'string', ['number']) as (id: number) => string,
    // Instrument enumeration
    instrumentCount: m.cwrap('bach_instrument_count', 'number', []) as () => number,
    instrumentName: m.cwrap('bach_instrument_name', 'string', ['number']) as (id: number) => string,
    // Character enumeration
    characterCount: m.cwrap('bach_character_count', 'number', []) as () => number,
    characterName: m.cwrap('bach_character_name', 'string', ['number']) as (id: number) => string,
    // Key enumeration
    keyCount: m.cwrap('bach_key_count', 'number', []) as () => number,
    keyName: m.cwrap('bach_key_name', 'string', ['number']) as (id: number) => string,
    // Scale enumeration
    scaleCount: m.cwrap('bach_scale_count', 'number', []) as () => number,
    scaleName: m.cwrap('bach_scale_name', 'string', ['number']) as (id: number) => string,
    // Default instrument
    defaultInstrumentForForm: m.cwrap('bach_default_instrument_for_form', 'number', ['number']) as (
      formId: number,
    ) => number,
    // Error handling
    errorString: m.cwrap('bach_error_string', 'string', ['number']) as (error: number) => string,
    // Version
    version: m.cwrap('bach_version', 'string', []) as () => string,
  };
}
