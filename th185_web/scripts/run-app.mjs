#!/usr/bin/env node
// Smoke test for the browser application module.
//
// The app module is built with -sENVIRONMENT=web, but Emscripten's web build
// still runs under Node when given the right globals. This probe proves the
// module loads, initializes against the real archive, and ticks without faults
// -- the properties the web shell depends on.
//
// What it does NOT prove: that pixels appear. There is no renderer yet.

import { readFileSync, existsSync } from 'node:fs';
import { resolve, dirname, basename } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '..');
const modulePath = resolve(root, 'artifacts', 'cpp-check', 'th185-app.mjs');

if (!existsSync(modulePath)) {
  console.error('error: app module not built: ' + modulePath);
  console.error('run: node th185_web/scripts/build-cpp.mjs --app');
  process.exit(2);
}

const dataPath = process.argv[2];
if (!dataPath) {
  console.error('usage: node scripts/run-app.mjs <path-to-th185.dat>');
  process.exit(2);
}
const archive = readFileSync(resolve(dataPath));

const createModule = (await import('file://' + modulePath.replaceAll('\\', '/'))).default;

let module;
try {
  module = await createModule({
    print: (text) => console.log('[wasm] ' + text),
    printErr: (text) => console.error('[wasm] ' + text),
    locateFile: (path) => resolve(dirname(modulePath), path),
  });
} catch (error) {
  console.error('FAIL module load: ' + error.message);
  process.exit(1);
}

// Place the archive where th185_initialize() looks for it.
module.FS.writeFile('/th185.dat', new Uint8Array(archive), { canOwn: true });

const initialized = module._th185_initialize();
if (!initialized) {
  const message = module.UTF8ToString(module._th185_error());
  console.error('FAIL initialize: ' + message);
  process.exit(1);
}
console.log('initialize        : ok');
console.log('runnable subs     : ' + module._th185_runnable_subroutines());

if (!module._th185_start()) {
  console.error('FAIL start: ' + module.UTF8ToString(module._th185_error()));
  process.exit(1);
}
console.log('start             : ok');

// Run a bounded number of ticks so the probe finishes quickly.
const TICKS = 60;
let failed = false;
for (let i = 0; i < TICKS; ++i) {
  if (!module._th185_tick(0)) {
    failed = true;
    break;
  }
}

if (failed) {
  console.error('FAIL tick: ' + module.UTF8ToString(module._th185_error()));
  process.exit(1);
}

const instructions = module._th185_ecl_instructions();
const faults = module._th185_ecl_faults();
const depth = module._th185_max_stack_depth();
console.log('ticks run         : ' + module._th185_ticks());
console.log('ecl instructions  : ' + instructions);
console.log('ecl faults        : ' + faults);
console.log('max stack depth   : ' + depth);
console.log('phase             : ' + module._th185_phase());

if (faults !== 0) {
  console.error('FAIL: ' + faults + ' ECL fault(s)');
  process.exit(1);
}
if (instructions === 0) {
  console.error('FAIL: no ECL instructions executed');
  process.exit(1);
}
console.log('');
console.log('selftest passed: the application module loads, initializes and ticks');
