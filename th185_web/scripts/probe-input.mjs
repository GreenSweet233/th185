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
//
// The field is cleared first because enemies now fire too: counting all bullets
// would measure the enemy's output, not the retirement rule.
// The scene controller keeps scheduling enemies, and enemies fire -- so this
// test has to run with nothing else producing bullets. Clearing and then
// ticking a short while is not enough: a scheduled enemy would fire again.
//
// Instead the player's own bullets are checked immediately after they leave the
// field, before any enemy can spawn: 120 frames is longer than a shot's flight
// time (it crosses ~400 units at 14/frame in ~29 frames) but the schedule's
// first entry is at frame 60, so enemies do appear. The assertion therefore
// counts only NON-hostile bullets.
module._th185_reset_scenario();
tick(0, 120);
const aliveLater = module._th185_bullets_alive();
const hostileLater = module._th185_hostile_bullets_alive();
console.log('after 120f  : alive ' + aliveLater + ' (hostile ' + hostileLater + ')');
check('player bullets retire when off-screen', aliveLater - hostileLater === 0);

// Collision: bullets must hit enemies, and enemies must die.
//
// Earlier phases of this probe run for hundreds of frames, and the wave spawned
// at initialise is destroyed long before this point -- so a fresh wave is
// spawned here. Reading the count without doing that is what made the first
// version of this test report a false failure.
// Start from a known state: earlier phases left the player at the left edge and
// spawned a wave that is already destroyed, so without this the collision test
// measured nothing.
module._th185_reset_scenario();
module._th185_spawn_test_wave(16);
console.log('recentre    : x=' + module._th185_player_x().toFixed(1)
            + ' enemies=' + module._th185_enemies_alive());
const enemiesBefore = module._th185_enemies_alive();
const killedBefore = module._th185_enemies_killed();
console.log('enemies     : alive ' + enemiesBefore + ', killed ' + killedBefore);

// Aim at the middle of the field and hold shoot for a while.
tick(0, 1);
tick(SHOOT, 180);
const enemiesAfter = module._th185_enemies_alive();
const killedAfter = module._th185_enemies_killed();
const hits = module._th185_collision_hits();
console.log('after fire  : alive ' + enemiesAfter + ', killed ' + killedAfter
            + ', hits ' + hits);

check('there were enemies to shoot', enemiesBefore > 0);
check('bullets hit enemies', hits > 0);
check('enemies died', killedAfter > killedBefore);
// The count no longer has to drop: 28 top-level coroutines (main plus 27
// BossNNtest controllers) all schedule waves, so respawns can outpace kills.
// What still holds is that kills accumulated.
check('kills accumulated while firing', killedAfter > killedBefore);

// Spawning must work through the exported hook. Measured on a clean field, not
// after the shooting phase -- that phase ends with zero enemies by design, so
// comparing against it proves nothing.
module._th185_reset_scenario();
const spawned = module._th185_spawn_test_wave(8);
const afterSpawn = module._th185_enemies_alive();
console.log('spawn wave  : +' + spawned + ' -> alive ' + afterSpawn);
check('test wave can be spawned', spawned === 8);
check('enemy count matches what was spawned', afterSpawn === spawned);

// Enemy fire and the player's lives.
//
// This closes the loop: enemies shoot, hostile bullets reach the player, lives
// drop, and invulnerability stops a single frame from draining everything.
module._th185_reset_scenario();
module._th185_spawn_test_wave(6);
const livesStart = module._th185_player_lives();
console.log('lives start : ' + livesStart);

let framesRun = 0;
for (; framesRun < 900; ++framesRun) {
  if (!module._th185_tick(0)) break;
  if (module._th185_player_lives() === 0) break;
}
const livesEnd = module._th185_player_lives();
const hitsTaken = module._th185_player_hits_taken();
const enemyShots = module._th185_enemy_shots();
console.log('after ' + framesRun + 'f : lives=' + livesEnd
            + ' hits=' + hitsTaken + ' enemyShots=' + enemyShots);

check('enemies fired at the player', enemyShots > 0);
check('the player took hits', hitsTaken > 0);
check('the player lost lives', livesEnd < livesStart);
check('invulnerability prevents instant death', hitsTaken < 10);

// ECL-driven enemy spawning.
//
// op1013 in the scene controller is a wave schedule: "spawn wave slot at these
// frame numbers". This is the first place the ECL data actually drives the game
// instead of being merely recorded.
module._th185_reset_scenario();
const schedTotal = module._th185_schedules_total();
console.log('schedules   : total=' + schedTotal);

// The earliest schedule in world01t fires at frame 60, so 400 frames covers
// several entries.
tick(0, 400);
const schedFired = module._th185_schedules_fired();
const neSched = module._th185_enemies_alive();
console.log('after 400f  : fired=' + schedFired + ' enemies=' + neSched);

check('the scene controller declared schedules', schedTotal > 0);
check('schedules fired over time', schedFired > 0);
check('scheduled enemies exist', neSched > 0);

// Scoring.
//
// The score cap is read from the executable (Difficulty::score_cap_for_stage
// mirrors `imul eax, [0x4D1004]`), so it is a real rule rather than a guess:
// it must be non-zero on every stage and grow with progression. The point
// values themselves are inferred -- TH185 has no Replay to diff against.
module._th185_load_stage(0);
const cap0 = module._th185_score_cap();
module._th185_load_stage(1);
const cap1 = module._th185_score_cap();
console.log('score caps  : stage0=' + cap0 + ' stage1=' + cap1);

check('the score cap is non-zero on stage 0', cap0 > 0);
check('the score cap grows with the stage', cap1 > cap0);

module._th185_reset_scenario();
tick(SHOOT, 600);
const score = module._th185_score();
const kills = module._th185_score_kills();
const grazes = module._th185_grazes();
const capped = module._th185_score_capped();
console.log('scoring     : score=' + score + ' kills=' + kills
            + ' grazes=' + grazes + ' capped=' + capped);

check('killing enemies scores', score > 0);
check('kills are counted', kills > 0);
check('the score stays under the cap', capped === 0 || score <= cap1);

// Difficulty.
//
// The 8 tiers are named in the executable; the score-cap table has 5 real
// entries and tiers 5..7 read past it (undefined in the original). The
// builtin slots 41 and 47..50 are the ones the ECL scripts branch on.
const diffCaps = [];
for (let d = 0; d < 8; ++d) {
  module._th185_set_difficulty(d);
  module._th185_load_stage(0);
  diffCaps.push(module._th185_score_cap());
}
module._th185_set_difficulty(2);
module._th185_load_stage(0);
const slot41 = module._th185_builtin_slot(41);
const isDiff = [0, 1, 2, 3].map((i) => module._th185_builtin_slot(47 + i));
console.log('difficulty  : caps=' + JSON.stringify(diffCaps));
console.log('              slot41=' + slot41 + ' is0..3=' + JSON.stringify(isDiff));

check('every difficulty has a non-zero score cap', diffCaps.every((c) => c > 0));
check('difficulty 0 caps lower than difficulty 3', diffCaps[0] < diffCaps[3]);
check('the difficulty slot reflects the selection', slot41 === 2);
check('only the matching is_difficulty_N slot is set',
      isDiff.join(',') === '0,0,1,0');

// Bombs.
//
// A bomb clears every hostile bullet (scoring the cancels) and damages every
// enemy. The stock is 3; a fourth press must do nothing.
module._th185_load_stage(1);
module._th185_reset_scenario();
tick(0, 400);
const preBombs = module._th185_bombs_left();
const preBullets = module._th185_bullets_alive();
const preEnemies = module._th185_enemies_alive();
module._th185_tick(BOMB);
module._th185_tick(0);
const postBullets = module._th185_bullets_alive();
const postBombs = module._th185_bombs_left();
console.log('bomb        : bullets ' + preBullets + ' -> ' + postBullets
            + ', enemies ' + preEnemies + ', stock ' + preBombs + ' -> ' + postBombs);

check('a bomb clears hostile bullets', postBullets < preBullets);
check('a bomb consumes stock', postBombs === preBombs - 1);

module._th185_reset_scenario();
for (let i = 0; i < 4; ++i) { module._th185_tick(BOMB); module._th185_tick(0); }
console.log('bomb stock  : left=' + module._th185_bombs_left()
            + ' used=' + module._th185_bombs_used());
check('the stock cannot go negative', module._th185_bombs_left() === 0);
check('only three bombs are usable', module._th185_bombs_used() === 3);

// Items.
//
// Enemies drop pickups on death. There is no item.anm in the archive; the
// icons live in front.anm (sprites 7..18), confirmed by decoding the atlas.
module._th185_load_stage(1);
module._th185_reset_scenario();
module._th185_spawn_test_wave(16);
tick(SHOOT, 400);
const itemsSpawned = module._th185_score_kills();
const itemsCollected = module._th185_items_collected();
const frags = module._th185_life_fragments();
console.log('items       : kills=' + itemsSpawned + ' collected=' + itemsCollected
            + ' lifeFragments=' + frags);

check('killing enemies drops items', itemsSpawned > 0);
check('items fall and are collected', itemsCollected > 0);
check('life fragments are counted', frags > 0);

// Death and respawn.
//
// take_hit() decrements the life; a respawn is a position reset plus the
// invulnerability window. Losing the last life ends the run.
module._th185_load_stage(1);
module._th185_reset_scenario();
tick(0, 1200);
const deaths = module._th185_deaths();
const over = module._th185_game_over();
const finalLives = module._th185_player_lives();
console.log('death       : deaths=' + deaths + ' lives=' + finalLives
            + ' gameOver=' + over);

check('the player can die', deaths > 0);
check('losing every life ends the run', over === 1 && finalLives === 0);

// Pickup effects: stars score, fragments count toward a life.
module._th185_load_stage(1);
module._th185_reset_scenario();
module._th185_spawn_test_wave(16);
const scoreBeforePickups = module._th185_score();
tick(SHOOT, 400);
const scoreAfterPickups = module._th185_score();
console.log('pickups     : score ' + scoreBeforePickups + ' -> ' + scoreAfterPickups
            + ', collected=' + module._th185_items_collected());

check('collecting stars adds score', scoreAfterPickups > scoreBeforePickups);

// Sound effects.
//
// The se_*.wav files inside th185.dat are ordinary RIFF/WAVE containers
// (unlike the BGM, which sits in thbgm.dat behind a ZWAV header). Both mono
// and stereo, 8 and 16 bit appear in the shipped data.
module._th185_load_stage(1);
const soundCount = module._th185_sounds_loaded();
const rates = [];
const channels = [];
for (let i = 0; i < soundCount; ++i) {
  rates.push(module._th185_sound_rate(i));
  channels.push(module._th185_sound_channels(i));
}
console.log('sounds      : loaded=' + soundCount + ' rates=' + JSON.stringify(rates)
            + ' channels=' + JSON.stringify(channels));

check('sound effects load from the archive', soundCount === 8);
check('every sample has a sane rate', rates.every((r) => r === 22050 || r === 44100));
check('mono and stereo samples both decode',
      channels.includes(1) && channels.includes(2));

module._th185_reset_scenario();
module._th185_spawn_test_wave(16);
tick(SHOOT, 400);
const played = module._th185_sounds_played();
const dropped = module._th185_sounds_dropped();
const throttled = module._th185_sounds_throttled();
console.log('sound mix   : played=' + played + ' dropped=' + dropped
            + ' throttled=' + throttled);

check('game events start sounds', played > 0);
check('throttling prevents voice starvation', dropped === 0);

// BGM index.
//
// thbgm.fmt lives inside th185.dat and describes the tracks in thbgm.dat.
// Parsing it does not need the 255 MB stream, so the index is checked here;
// the stream itself is exercised by export-bgm.mjs.
const bgmTracks = module._th185_bgm_tracks();
console.log('bgm index   : tracks=' + bgmTracks);
check('the BGM index parses', bgmTracks === 10);

// Audio output.
//
// The shell pulls one tick of mixed stereo audio after each tick. Two mixers
// write into the same buffer (sound effects and BGM), so the first clears and
// the second only adds -- a clearing BGM mixer erased the effects, which showed
// up as played>0 with an all-zero buffer.
module._th185_load_stage(1);
module._th185_reset_scenario();
module._th185_spawn_test_wave(16);

let audioNonZero = 0;
let audioPeak = 0;
for (let i = 0; i < 200; ++i) {
  module._th185_tick(SHOOT);
  const pointer = module._th185_audio_frame();
  const samples = module._th185_audio_frame_samples();
  if (!pointer || !samples) continue;
  const base = pointer >> 2;
  for (let k = 0; k < samples; ++k) {
    const v = Math.abs(module.HEAPF32[base + k]);
    if (v > 0.001) ++audioNonZero;
    if (v > audioPeak) audioPeak = v;
  }
}
console.log('audio       : samples/tick=' + module._th185_audio_frames_per_tick()
            + ' rate=' + module._th185_audio_rate()
            + ' nonZero=' + audioNonZero + ' peak=' + audioPeak.toFixed(2));

check('the mixer produces 367 frames per tick at 22050 Hz',
      module._th185_audio_frames_per_tick() === 367 &&
      module._th185_audio_rate() === 22050);
check('the audio buffer is not silent', audioNonZero > 0);
check('the audio stays inside -1..1', audioPeak <= 1.0);

// Boss scripts.
//
// bossNN.ecl are separate files that worldNN.ecl never names. They hold 264
// subroutines (Boss01, Boss01Boss1, Boss01BossCard1, ...). Loading them makes
// the subroutines reachable; they are not yet DRIVEN, because only main runs
// as a top-level coroutine.
module._th185_load_stage(0);
const subs = module._th185_sub_count();
console.log('boss scripts: subroutines=' + subs);
check('boss scripts are registered (scene + default + waves + bosses)',
      subs > 700);

// Boss scripts driven.
//
// The boss SCENE controller is BossNNtest, not BossNN: Boss01test opens with
// op40, carries nine op1013 wave schedules, then op300 Call Boss01 (the body)
// and op11 Call WaveSync, then waits on builtin slot 104 with the op1019 /
// op1017 pair. Running BossNN as well would run the body twice.
//
// BossNN is an entry subroutine, not a helper: nothing outside its own file
// calls it, and it opens with the same prologue main uses (op40 / op502 / op30
// / op1006 / op1024 / op307). Running all 27 plus main gives 28 top-level
// coroutines, and op1024 sets each boss's index.
module._th185_load_stage(0);
module._th185_reset_scenario();
tick(0, 300);
const bossSets = module._th185_boss_index_sets();
const active = module._th185_active_script_count();
console.log('boss        : active=' + active + ' indexSets=' + bossSets);
check('every boss scene controller runs (main + 27 BossNNtest)', active === 28);
// BossNNtest is the controller: it schedules waves first and only then calls
// the boss body with op300, so op1024 (inside the body) fires later than the
// controller starts. Assert that the mechanism works, not a fixed count.
check('op1024 (boss index) fires once the body is reached', bossSets > 0);

// Boss opcode coverage.
//
// The boss scripts run in a chain: each opcode implemented moves
// firstUnsupported deeper. These counters prove the newly implemented ones
// actually execute rather than just being listed as supported.
module._th185_load_stage(0);
module._th185_reset_scenario();
tick(0, 400);
const effectSlots = module._th185_effect_slot_calls();
const phases = module._th185_boss_phases();
const posPairs = module._th185_position_pairs();
const unsup = module._th185_first_unsupported();
console.log('boss ops    : effectSlot=' + effectSlots + ' phase=' + phases
            + ' posPair=' + posPairs + ' firstUnsupported=' + unsup);
check('op400 (effect slot) executes', effectSlots > 0);
check('op503 (boss phase) executes', phases > 0);
check('op501 (position pair) executes', posPairs > 0);
check('the boss chain has advanced past op400', unsup !== 400);

// Boss spawn boxes.
//
// op504 sets a spawn area (centre plus field-scale extents 280x64). It runs
// once per boss frame, so its counter grows quickly once the boss chain
// reaches it.
const spawnBoxes = module._th185_spawn_boxes();
const unsup2 = module._th185_first_unsupported();
console.log('boss spawn  : spawnBoxes=' + spawnBoxes
            + ' firstUnsupported=' + unsup2);
check('op504 (boss spawn box) executes', spawnBoxes > 0);
check('the boss chain has advanced past op504', unsup2 !== 504);

console.log('');
if (failures) {
  console.error('probe failed: ' + failures + ' item(s)');
  process.exit(1);
}
console.log('probe passed: input drives the player');
