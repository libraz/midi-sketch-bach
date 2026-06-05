import {
  BachGenerator,
  getCharacters,
  getDefaultInstrumentForForm,
  getForms,
  getScales,
  init,
} from '../dist/index.mjs';

const wasmPath = new URL('../dist/bach.wasm', import.meta.url).pathname;

const organForms = new Set([
  'fugue',
  'prelude_and_fugue',
  'trio_sonata',
  'chorale_prelude',
  'toccata_and_fugue',
  'passacaglia',
  'fantasia_and_fugue',
]);

const incompatibleCharactersByForm = {
  chorale_prelude: new Set(['playful', 'restless']),
  toccata_and_fugue: new Set(['noble']),
};

function compatibleCharacterFor(formName, characters) {
  const incompatible = incompatibleCharactersByForm[formName] || new Set();
  const character = characters.find((c) => !incompatible.has(c.name));
  if (!character) {
    throw new Error(`No compatible character found for ${formName}`);
  }
  return character;
}

function expectThrows(config) {
  const generator = new BachGenerator();
  try {
    generator.generate(config);
  } catch {
    generator.destroy();
    return;
  }
  generator.destroy();
  throw new Error(`Expected generation to fail for ${JSON.stringify(config)}`);
}

await init({ wasmPath });

const forms = getForms();
const scales = getScales();
const characters = getCharacters();

if (forms.length !== 10) {
  throw new Error(`Expected 10 forms, got ${forms.length}`);
}
if (scales.length !== 4) {
  throw new Error(`Expected 4 scales, got ${scales.length}`);
}

let generated = 0;

for (const form of forms) {
  for (const scale of scales) {
    const generator = new BachGenerator();
    const config = {
      form: form.id,
      key: 0,
      isMinor: false,
      bpm: 100,
      seed: 20260606 + form.id * 10 + scale.id,
      instrument: getDefaultInstrumentForForm(form.id),
      scale: scale.id,
    };

    if (organForms.has(form.name)) {
      config.numVoices = 3;
      config.character = compatibleCharacterFor(form.name, characters).id;
    }

    generator.generate(config);
    const midi = generator.getMidi();
    const events = generator.getEvents();
    const info = generator.getInfo();
    generator.destroy();

    if (midi.length === 0) {
      throw new Error(`Empty MIDI for form=${form.name} scale=${scale.name}`);
    }
    if (!events.tracks?.length) {
      throw new Error(`No tracks for form=${form.name} scale=${scale.name}`);
    }
    if (info.totalBars <= 0 || info.totalTicks <= 0) {
      throw new Error(`Invalid duration for form=${form.name} scale=${scale.name}`);
    }
    generated += 1;
  }
}

expectThrows({ form: 'chorale_prelude', character: 'playful', seed: 42 });
expectThrows({ form: 'chorale_prelude', character: 'restless', seed: 42 });
expectThrows({ form: 'toccata_and_fugue', character: 'noble', seed: 42 });

console.log(`demo_generation_smoke ok generated=${generated} forms=${forms.length} scales=${scales.length}`);
