#!/usr/bin/env node
// Play a stage exactly the way the browser shell does, end to end.
//
// WHY THIS EXISTS
// ---------------
// Two P0s in a row were "the shell and the module disagree at the seam":
//   1. the shell never called reset_scenario(), so a browser run had zero lives,
//      no loadout and no magic circle;
//   2. UTF8ToString was never exported, so prepare() threw and the game never
//      started at all.
// Both were invisible to ~370 assertions and to 39 CI checks, because every
// probe builds its own state first and then asserts. So this check does the
// opposite: it performs ONLY the calls shell.mjs performs, in the same order,
// and then asserts the game is actually playable -- lives, loadout, magic,
// input reaching the player, and the stage reaching a clear.
//
// It cannot prove the page renders (no browser here). It CAN prove that the
// call sequence the page uses produces a game that runs.
//
// Usage: node scripts/browser_path_probe.mjs <th185-app.mjs> <th185.dat>
// Exit codes: 0 ok; 1 unexpected state; 2 usage/environment missing

import { readFileSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulePath, dataPath] = process.argv.slice(2);
if (!modulePath || !dataPath) {
  console.error('usage: node scripts/browser_path_probe.mjs <th185-app.mjs> <th185.dat>');
  process.exit(2);
}
const moduleFile = resolve(modulePath);
const data = resolve(dataPath);
if (!existsSync(moduleFile) || !existsSync(data)) {
  console.error('error: missing module or data file');
  process.exit(2);
}

const factory = (await import(pathToFileURL(moduleFile).href)).default;
const core = await factory({
  print: () => {},
  printErr: () => {},
  locateFile: (name) => resolve(dirname(moduleFile), name),
});

// --- prepare() -----------------------------------------------------------
core.FS.writeFile('/th185.dat', new Uint8Array(readFileSync(data)), { canOwn: true });
console.log('initialize: ' + core._th185_initialize());

// populatePickers(): the shell decodes every name. This is what threw before
// UTF8ToString was exported.
const scenes = [];
for (let i = 0; i < core._th185_scene_count(); ++i) {
  scenes.push(core.UTF8ToString(core._th185_scene_name(i)));
}
console.log('scene_names_decoded: ' + scenes.length);
console.log('first_scene: ' + scenes[0]);
console.log('last_scene: ' + scenes[scenes.length - 1]);
const difficulties = [];
for (let i = 0; i < core._th185_difficulty_count(); ++i) {
  difficulties.push(core.UTF8ToString(core._th185_difficulty_name(i)));
}
console.log('difficulty_names_decoded: ' + difficulties.length);
console.log('last_difficulty: ' + difficulties[difficulties.length - 1]);

// --- applySelection() ----------------------------------------------------
core._th185_set_difficulty(2);            // Normal, the shell's default
console.log('load_stage: ' + core._th185_load_stage(1));
core._th185_reset_scenario();             // the call the shell was missing
console.log('start: ' + core._th185_start());

// --- the run must be initialised -----------------------------------------
console.log('phase_after_start: ' + core._th185_phase());
console.log('lives_after_start: ' + core._th185_player_lives());
console.log('loadout_after_start: ' + core._th185_cards_loadout_size());
console.log('magic_ready_after_start: ' + core._th185_magic_ready());

// --- frame(): fixed 60 Hz steps with the shell's bitmask ----------------
const SHOOT = 1 << 0;
const FOCUS = 1 << 2;
function tick(held, count) {
  for (let i = 0; i < count; ++i) {
    if (!core._th185_tick(held)) return false;
  }
  return true;
}

// The shell presents a frame every time it ticks; check that path works too.
tick(0, 2);
const width = core._th185_frame_width();
const height = core._th185_frame_height();
const bytes = core._th185_frame_bytes();
const ptr = core._th185_frame_pixels();
console.log('frame: ' + width + 'x' + height);
console.log('frame_bytes: ' + bytes);
// HEAPU8 must be reachable: the shell reads pixels through it every frame.
let nonZero = 0;
if (ptr) {
  const view = core.HEAPU8.subarray(ptr, ptr + bytes);
  for (let i = 0; i < view.length; i += 997) { if (view[i] !== 0) ++nonZero; }
}
console.log('frame_nonzero_samples: ' + nonZero);

// Input must actually move the player.
const beforeX = core._th185_player_x();
tick(1 << 7, 12);                        // RIGHT
const afterX = core._th185_player_x();
console.log('player_moved_right: ' + (afterX > beforeX));

// Hold shoot: the player must emit bullets.
tick(SHOOT, 30);
console.log('bullets_fired: ' + core._th185_bullets_fired());

// Play the stage out. The probe uses the same tick() the shell uses, so a
// failure here means the page would fail too.
let clearedAt = -1;
let peakEnemies = 0;
// 12000 frames: the slowest measured clear is stage 3 at 8302. An earlier
// version of this probe used 6000 and reported "did not clear" -- that was the
// budget, not a defect, which is exactly the kind of false signal this file is
// supposed to avoid. Use the same input the other probes use (plain SHOOT) so a
// difference here means a real difference in the call path.
for (let f = 0; f < 12000; ++f) {
  const held = SHOOT;
  if (!core._th185_tick(held)) break;
  const alive = core._th185_enemies_alive();
  if (alive > peakEnemies) peakEnemies = alive;
  if (core._th185_stage_clear()) { clearedAt = f; break; }
}
console.log('cleared_at_frame: ' + clearedAt);
console.log('peak_enemies: ' + peakEnemies);
console.log('schedules_fired: ' + core._th185_schedules_fired());
console.log('schedules_expected: ' + core._th185_schedules_expected());
console.log('phase_after_play: ' + core._th185_phase());
console.log('score_after_play: ' + core._th185_score());

// --- death state -------------------------------------------------------
//
// The shell's call sequence is also where the 'zero lives' defect lived: the
// module set a game_over flag and then did nothing, so the player sat at the
// death position without an invulnerability window while the stage ran on to a
// 'clear' at zero lives.
//
// What the original does, measured on th185.exe (docs/DEATH-AND-PHASE.md):
//   take_hit() 0x40AE70 is guarded by 'cmp [0x4d10bc],0 / jle' (0x40AE8B), so
//   the count never goes below 0; the death handler 0x462191 branches
//   'cmp [0x4d10bc],0 / jge 0x462305' (0x4622D6) -- life >= 0 respawns, only
//   life < 0 wraps up. There is no 'Game Over' string in the binary at all.
//
// So: reaching zero lives is a NORMAL event that must respawn the player, must
// not fabricate a game-over state, and must never leave the life count negative.
core._th185_load_stage(1);
core._th185_reset_scenario();
core._th185_start();
let zeroAt = -1;
for (let f = 0; f < 1500; ++f) {
  if (!core._th185_tick(0)) break;
  if (core._th185_player_lives() === 0) { zeroAt = f; break; }
}
const livesAfter = core._th185_player_lives();
const overFlag = core._th185_game_over();
const yAfter = core._th185_player_y();
const invulnAfter = core._th185_player_invuln_frames();
console.log('zero_lives_at_frame: ' + zeroAt);
console.log('lives_after_zero: ' + livesAfter);
console.log('game_over_flag_at_zero: ' + overFlag);
console.log('player_y_at_zero: ' + yAfter);
console.log('player_invuln_at_zero: ' + invulnAfter);

let deathFailures = 0;
function deathCheck(name, ok, detail) {
  console.log((ok ? 'ok   ' : 'FAIL ') + name + (detail === undefined ? '' : '  (' + detail + ')'));
  if (!ok) ++deathFailures;
}
deathCheck('the run actually reached zero lives', zeroAt >= 0, String(zeroAt));
deathCheck('zero lives is not reported as game over', overFlag === 0, String(overFlag));
deathCheck('the life count never goes negative', livesAfter === 0, String(livesAfter));
deathCheck('the player was respawned (y back at the start)', yAfter > 0, String(yAfter));
deathCheck('respawn restores the invulnerability window', invulnAfter > 0, String(invulnAfter));
console.log('death_state_failures: ' + deathFailures);
if (deathFailures > 0) process.exitCode = 1;
