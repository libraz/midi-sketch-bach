#!/usr/bin/env node
import * as esbuild from 'esbuild';
import { execSync } from 'node:child_process';
import {
  readFileSync,
  writeFileSync,
  unlinkSync,
  readdirSync,
  copyFileSync,
  mkdirSync,
  existsSync,
} from 'node:fs';
import { join } from 'node:path';
import ts from 'typescript';

const distDir = './dist';

// Step 0: Copy WASM files to dist
console.log('Copying WASM files...');
if (!existsSync(distDir)) {
  mkdirSync(distDir, { recursive: true });
}
copyFileSync('./build-wasm/bin/bach.js', join(distDir, 'bach.js'));
copyFileSync('./build-wasm/bin/bach.wasm', join(distDir, 'bach.wasm'));

// Step 1: Generate .d.ts files with tsc
console.log('Generating type declarations...');
execSync('npx tsc --emitDeclarationOnly', { stdio: 'inherit' });

// Plugin to rewrite WASM module import path
const wasmPlugin = {
  name: 'wasm-path-rewrite',
  setup(build) {
    // Mark the WASM module import as external and rewrite path
    build.onResolve({ filter: /\.\.\/bach\.js$/ }, () => ({
      path: './bach.js',
      external: true,
    }));
  },
};

// Common esbuild options
const commonOptions = {
  entryPoints: ['js/src/index.ts'],
  bundle: true,
  sourcemap: true,
  target: 'es2020',
  plugins: [wasmPlugin],
};

// Step 2: Bundle JS with esbuild (ESM)
console.log('Bundling ESM...');
await esbuild.build({
  ...commonOptions,
  format: 'esm',
  outfile: 'dist/index.mjs',
});

// Step 3: Bundle JS with esbuild (CJS)
console.log('Bundling CJS...');
await esbuild.build({
  ...commonOptions,
  format: 'cjs',
  outfile: 'dist/index.cjs',
});

// Step 4: Create bundled .d.ts file
console.log('Bundling type declarations...');

// The published declaration surface is defined entirely by index.ts's own
// `export { ... } from './mod'` / `export type { ... } from './mod'`
// clauses: whatever tsc records in dist/index.d.ts for those clauses is what
// gets inlined below, and nothing else. A module's other top-level
// declarations (e.g. internal.ts's EmscriptenModule/Api/getModule/getApi)
// never leak into the bundle because they were never asked for.
const indexDtsPath = join(distDir, 'index.d.ts');
const indexDts = readFileSync(indexDtsPath, 'utf-8');
const indexSourceFile = ts.createSourceFile('index.d.ts', indexDts, ts.ScriptTarget.Latest, true);

// Module specifier (e.g. './bach') -> Set of locally declared names it must
// publish, keyed in the order modules first appear in index.d.ts.
const publicNamesByModule = new Map();
for (const statement of indexSourceFile.statements) {
  if (!ts.isExportDeclaration(statement)) continue;
  if (!statement.moduleSpecifier || !ts.isStringLiteral(statement.moduleSpecifier)) continue;
  if (!statement.exportClause || !ts.isNamedExports(statement.exportClause)) continue;
  const specifier = statement.moduleSpecifier.text;
  const names = publicNamesByModule.get(specifier) ?? new Set();
  for (const element of statement.exportClause.elements) {
    // `export { local as alias }` publishes `alias`, but the declaration to
    // inline still lives under the module's local name.
    names.add((element.propertyName ?? element.name).text);
  }
  publicNamesByModule.set(specifier, names);
}

/** Returns the top-level name(s) a declaration statement introduces. */
const declaredNames = (statement) => {
  if (
    ts.isFunctionDeclaration(statement) ||
    ts.isClassDeclaration(statement) ||
    ts.isInterfaceDeclaration(statement) ||
    ts.isTypeAliasDeclaration(statement) ||
    ts.isEnumDeclaration(statement)
  ) {
    return statement.name ? [statement.name.text] : [];
  }
  if (ts.isVariableStatement(statement)) {
    return statement.declarationList.declarations
      .map((decl) => (ts.isIdentifier(decl.name) ? decl.name.text : undefined))
      .filter((name) => name !== undefined);
  }
  return [];
};

/** Recursively records every type-reference identifier that names a local declaration. */
const collectLocalReferences = (node, localNames, found) => {
  if (ts.isTypeReferenceNode(node) && ts.isIdentifier(node.typeName)) {
    if (localNames.has(node.typeName.text)) {
      found.add(node.typeName.text);
    }
  }
  ts.forEachChild(node, (child) => collectLocalReferences(child, localNames, found));
};

let combinedDts = `/**
 * midi-sketch-bach - Bach Instrumental MIDI Generator
 * @packageDocumentation
 */

`;

for (const [specifier, publicNames] of publicNamesByModule) {
  const fileName = `${specifier.replace(/^\.\//, '')}.d.ts`;
  const content = readFileSync(join(distDir, fileName), 'utf-8');
  const sourceFile = ts.createSourceFile(fileName, content, ts.ScriptTarget.Latest, true);

  // name -> declaring statement, for every top-level declaration in this module.
  const declarationsByName = new Map();
  for (const statement of sourceFile.statements) {
    for (const name of declaredNames(statement)) {
      declarationsByName.set(name, statement);
    }
  }

  // Resolve the requested public names plus any local type they depend on.
  const included = new Set();
  const worklist = [...publicNames];
  while (worklist.length > 0) {
    const name = worklist.pop();
    if (included.has(name)) continue;
    const statement = declarationsByName.get(name);
    if (!statement) {
      throw new Error(
        `dist/index.d.ts re-exports '${name}' from '${specifier}', but ${fileName} declares no such name.`,
      );
    }
    included.add(name);
    const references = new Set();
    collectLocalReferences(statement, declarationsByName, references);
    for (const reference of references) {
      if (!included.has(reference)) worklist.push(reference);
    }
  }

  // Emit the included declarations in their original source order,
  // deduplicated (one statement can declare more than one name).
  const emitted = new Set();
  let moduleDts = '';
  for (const statement of sourceFile.statements) {
    const names = declaredNames(statement);
    if (names.length === 0 || !names.some((name) => included.has(name))) continue;
    if (emitted.has(statement)) continue;
    emitted.add(statement);
    moduleDts += `${statement.getFullText(sourceFile).trim()}\n`;
  }

  if (moduleDts.length > 0) {
    combinedDts += `// From ${specifier.replace(/^\.\//, '')}.ts\n${moduleDts}\n`;
  }
}

// Loud guard: every name index.ts re-exports must have a matching declaration
// inlined above, so a gap between the public export surface and the emitted
// declarations fails the build instead of shipping unnoticed.
for (const publicNames of publicNamesByModule.values()) {
  for (const name of publicNames) {
    const isDeclared = new RegExp(
      `(?:^|\\n)\\s*export\\s+(?:declare\\s+)?(?:abstract\\s+)?(?:class|function|interface|type|enum|const|let|var)\\s+${name}\\b`,
    );
    if (!isDeclared.test(combinedDts)) {
      throw new Error(
        `Bundled dist/index.d.ts is missing a declaration for '${name}', which index.ts re-exports.`,
      );
    }
  }
}

writeFileSync(join(distDir, 'index.d.ts'), combinedDts);

// Clean up individual .d.ts files (keep only index.d.ts)
for (const file of readdirSync(distDir)) {
  if (file.endsWith('.d.ts') && file !== 'index.d.ts') {
    unlinkSync(join(distDir, file));
  }
  if (file.endsWith('.d.ts.map')) {
    unlinkSync(join(distDir, file));
  }
  // Remove individual .js files (we now have bundled index.mjs/index.cjs)
  if (file.endsWith('.js') && file !== 'bach.js') {
    unlinkSync(join(distDir, file));
  }
  if (file.endsWith('.js.map') && file !== 'bach.js.map') {
    unlinkSync(join(distDir, file));
  }
}

console.log('Build complete!');
console.log('  dist/index.mjs  - ESM bundle');
console.log('  dist/index.cjs  - CJS bundle');
console.log('  dist/index.d.ts - Type declarations');
