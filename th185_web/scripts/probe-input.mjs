#!/usr/bin/env node
// Drive the application with real input and report the player's response.
//
// This is the check that input actually reaches something. Before the player
// existed, `tick(held)` recorded a bitmask and nothing consumed it; this probe
// proves the bits now move an entity.

import { readFileSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulePath, dataPath] = process.argv.slice(2);
if (!modulePath || !dataPath) {
  console.error('usage: node scripts/probe-input.mjs <th185-app.mjs> <th185.dat>');
  process.exit(2);
}

const moduleFile = resolve(modulePath);
const data = resolve(dataPath);
if (!existsSync(moduleFile) || !existsSync(data)) {
  console.error('error: missing module or data file');
  process.exit(2);
}

const factory = (await import(pathToFileURL(moduleFile).href)).default;
const module = await factory({
  print: (line) => process.stdout.write(line + '\n'),
  printErr: (line) => process.stderr.write(line + '\n'),
  locateFile: (name) => resolve(dirname(moduleFile), name),
});

module.FS.writeFile('/th185.dat', new Uint8Array(readFileSync(data)), { canOwn: true });
if (!module._th185_initialize()) {
  console.error('FAIL initialize: ' + module.UTF8ToString(module._th185_error()));
  process.exit(1);
}
if (!module._th185_start()) {
  console.error('FAIL start: ' + module.UTF8ToString(module._th185_error()));
  process.exit(1);
}

const SHOOT = 1 << 0;
const BOMB = 1 << 1;
const FOCUS = 1 << 2;
const UP = 1 << 4;
const DOWN = 1 << 5;
const LEFT = 1 << 6;
const RIGHT = 1 << 7;

function tick(held, count) {
  for (let i = 0; i < count; ++i) {
    if (!module._th185_tick(held)) {
      console.error('FAIL tick: ' + module.UTF8ToString(module._th185_error()));
      process.exit(1);
    }
  }
}

const startX = module._th185_player_x();
const startY = module._th185_player_y();
console.log('start       : x=' + startX.toFixed(1) + ' y=' + startY.toFixed(1));

// Idle: position must not drift.
tick(0, 10);
const idleX = module._th185_player_x();
const idleY = module._th185_player_y();
console.log('idle        : x=' + idleX.toFixed(1) + ' y=' + idleY.toFixed(1));

// Move left: x must decrease.
tick(LEFT, 10);
const leftX = module._th185_player_x();
console.log('move left   : x=' + leftX.toFixed(1));

// Move right for longer: x must increase past the start.
tick(RIGHT, 20);
const rightX = module._th185_player_x();
console.log('move right  : x=' + rightX.toFixed(1));

// Focus must halve the speed.
const beforeFocus = module._th185_player_x();
tick(FOCUS | LEFT, 10);
const afterFocus = module._th185_player_x();
const focusDelta = Math.abs(afterFocus - beforeFocus);
const beforeNormal = module._th185_player_x();
tick(LEFT, 10);
const afterNormal = module._th185_player_x();
const normalDelta = Math.abs(afterNormal - beforeNormal);
console.log('focus speed : ' + focusDelta.toFixed(2) + ' vs normal ' + normalDelta.toFixed(2));
console.log('focused     : ' + module._th185_player_focused());

// Hold left for a long time: must clamp, not run off the field.
tick(LEFT, 300);
const clampedX = module._th185_player_x();
console.log('clamped     : x=' + clampedX.toFixed(1));

// Diagonal must not be faster than straight.
tick(0, 1);
const diagStart = { x: module._th185_player_x(), y: module._th185_player_y() };
tick(UP | RIGHT, 10);
const diagDX = module._th185_player_x() - diagStart.x;
const diagDY = module._th185_player_y() - diagStart.y;
const diagDistance = Math.hypot(diagDX, diagDY);
console.log('diagonal    : distance=' + diagDistance.toFixed(2));

// Button edges.
tick(SHOOT, 1);
tick(SHOOT, 5);
const shots = module._th185_player_shots();
tick(BOMB, 1);
tick(BOMB, 5);
const bombs = module._th185_player_bombs();
console.log('shots       : ' + shots);
console.log('bombs       : ' + bombs);

// Assertions.
let failures = 0;
function check(label, condition) {
  console.log('[' + (condition ? 'PASS' : 'FAIL') + '] ' + label);
  if (!condition) ++failures;
}

check('idle does not move the player', Math.abs(idleX - startX) < 0.001);
check('left decreases x', leftX < startX);
check('right increases x', rightX > leftX);
check('focus is slower than normal', focusDelta < normalDelta * 0.75);
check('player is clamped to the field', clampedX >= -193 && clampedX <= -191);
check('diagonal is not faster than straight', diagDistance <= 4.0 * 10 + 0.5);
check('one shoot press counts once', shots === 1);
check('one bomb press counts once', bombs === 1);

// Bullets: holding shoot must fire repeatedly, and bullets must move up.
const firedBefore = module._th185_bullets_fired();
tick(SHOOT, 20);
const firedAfter = module._th185_bullets_fired();
const aliveAfter = module._th185_bullets_alive();
console.log('bullets     : fired ' + firedBefore + ' -> ' + firedAfter
            + ', alive ' + aliveAfter);
check('holding shoot keeps firing', firedAfter > firedBefore + 4);
check('bullets are alive on screen', aliveAfter > 0);

// Bullets must leave the screen and be retired.
tick(0, 120);
const aliveLater = module._th185_bullets_alive();
console.log('after 120f  : alive ' + aliveLater);
check('bullets retire when off-screen', aliveLater === 0);

console.log('');
if (failures) {
  console.error('probe failed: ' + failures + ' item(s)');
  process.exit(1);
}
console.log('probe passed: input drives the player');
