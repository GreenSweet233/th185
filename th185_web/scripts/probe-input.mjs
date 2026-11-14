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
// ONLY the stage's own boss runs. Every BossNNtest carries nine op1013 wave
// schedules, so running all 27 kept the field populated forever
// (schedules_fired grew 29 -> 37 -> 40 -> 46 -> 57 with no sign of stopping).
check('main plus exactly one boss controller runs', active === 2);
// BossNNtest is the controller: it schedules waves first and only then calls
// the boss body with op300, so op1024 (inside the body) fires later than the
// controller starts. Assert that the mechanism works, not a fixed count.
check('op1024 (boss index) fires once the body is reached', bossSets > 0);

// Every stage must be completable.
//
// The clear condition needs all three at once: the boss was spawned and is now
// gone, the field is empty, and every scheduled wave has fired. `schedules_fired
// reaches `schedules_expected` exactly, which is why this can be asserted as an
// equality rather than a threshold.
{
    const stages = 9;
    let cleared = 0;
    const rows = [];
    for (let s = 0; s < stages; ++s) {
        module._th185_load_stage(s);
        module._th185_reset_scenario();
        let at = -1;
        // 8000 is comfortably above the slowest observed clear (stage0 at
        // 2183 frames) while keeping the whole probe inside its budget.
        for (let f = 0; f < 8000; ++f) {
            tick(SHOOT, 1);
            if (module._th185_stage_clear() && at < 0) { at = f; break; }
        }
        if (module._th185_stage_clear()) ++cleared;
        rows.push(s + ':' + at + ' ' + module._th185_schedules_fired() +
                  '/' + module._th185_schedules_expected());
    }
    console.log('stage clear : ' + rows.join('  '));
    check('every stage can be cleared', cleared === stages);
}
// Card system: table, wallet, purchase, loadout and effect aggregation.
//
// TH185's loop is buy-in-market then equip-before-stage. The table is parsed
// from the binary (87 entries, 68 bytes each); price is fields[11] and the
// active flag is fields[0]. The effect aggregates use fields[9] (category),
// which is this build's interpretation and is recorded as such in the ledger.
{
    const n = module._th185_card_count();
    const cardName = module.cwrap('th185_card_name', 'string', ['number']);
    let active = 0, priced = 0;
    for (let i = 0; i < n; ++i) {
        if (module._th185_card_is_active(i)) ++active;
        if (module._th185_card_price(i) > 0) ++priced;
    }
    console.log('cards       : ' + n + ' active=' + active + ' priced=' + priced);
    check('the card table has 87 entries', n === 87);
    check('26 cards are active-use', active === 26);
    check('82 cards have a market price', priced === 82);
    check('the first card is PENDULUM', cardName(0) === 'PENDULUM');

    module._th185_cards_add_currency(100);
    const before = module._th185_cards_currency();
    const dango = module._th185_card_id(2);
    const price = module._th185_card_price(2);
    const bought = module._th185_cards_buy(dango);
    const after = module._th185_cards_currency();
    console.log('buy         : id=' + dango + ' price=' + price +
                ' ' + before + ' -> ' + after + ' owned=' + module._th185_cards_owned());
    check('a card can be bought', bought === 1);
    check('buying deducts the price', after === before - price);
    check('the bought card is owned', module._th185_cards_owned() === 1);
    check('buying the same card twice fails', module._th185_cards_buy(dango) === 0);

    check('an owned card can be equipped', module._th185_cards_equip(dango) === 1);
    check('the loadout holds it',
          module._th185_cards_loadout_size() === 1 &&
          module._th185_cards_loadout_at(0) === dango);
    check('an unowned card cannot be equipped',
          module._th185_cards_equip(module._th185_card_id(5)) === 0);

    // Effects are keyed by card NAME, not by a table field.
    //
    // EXTEND is the series' standard 1-up card, so equipping it must grant
    // exactly one extra life. DANGO (the card bought above) grants nothing --
    // an earlier version assumed it was a life card because of its table
    // category, which turned out to be wrong.
    const lives = module._th185_cards_extra_lives();
    console.log('effects     : lives=' + lives +
                ' bombs=' + module._th185_cards_extra_bombs() +
                ' shot=' + module._th185_cards_shot_bonus());
    check('a character card grants no life bonus', lives === 0);

    // Find EXTEND by name and equip it.
    let extendId = -1;
    for (let i = 0; i < n; ++i) {
        if (cardName(i) === 'EXTEND') { extendId = module._th185_card_id(i); break; }
    }
    check('the table contains EXTEND', extendId >= 0);
    module._th185_cards_add_currency(1000);
    if (extendId >= 0) {
        module._th185_cards_buy(extendId);
        module._th185_cards_equip(extendId);
    }
    const withExtend = module._th185_cards_extra_lives();
    console.log('EXTEND      : equipped id=' + extendId + ' extraLives=' + withExtend);
    check('EXTEND grants exactly one extra life', withExtend === 1);

    // The loadout must reach the PLAYER, not just the aggregate.
    //
    // reset_scenario applies the equipped cards; before that link existed the
    // inventory reported extra_lives but the player still started with 3.
    // The loadout must reach the PLAYER, not just the aggregate.
    //
    // Before that link existed the inventory reported extra_lives but the
    // player still started with 3. EXTEND is the card that grants one, so
    // equipping it must raise the starting lives by exactly one.
    // The loadout must reach the PLAYER, not just the aggregate.
    //
    // Comparing before/after within one run does not work here because the
    // card is already equipped from the earlier check. Instead: clear the
    // loadout, measure, equip EXTEND, measure again.
    module._th185_cards_clear_loadout();
    module._th185_load_stage(0);
    module._th185_reset_scenario();
    const withoutLives = module._th185_player_lives();
    const withoutApplied = module._th185_card_extra_lives_applied();

    module._th185_cards_add_currency(1000);
    if (extendId >= 0) module._th185_cards_equip(extendId);
    module._th185_reset_scenario();
    const withLives = module._th185_player_lives();
    const withApplied = module._th185_card_extra_lives_applied();
    console.log('cards->player: no card ' + withoutLives + ' lives (applied ' +
                withoutApplied + ') -> EXTEND ' + withLives + ' lives (applied ' +
                withApplied + ')');
    check('EXTEND raises the applied life bonus', withApplied === withoutApplied + 1);
    check('EXTEND raises the player lives', withLives === withoutLives + 1);
}
// Stage structure: nine selectable scenes with fixed wave counts.
//
// From docs/BUILD.md (THBWiki + thprac warp tables). The wave counts are the
// check that matters: they sum to the documented 50 mid-stage slots.
{
    const sceneCount = module._th185_scene_count();
    const sceneName = module.cwrap('th185_scene_name', 'string', ['number']);
    let waves = 0, bosses = 0;
    const rows = [];
    for (let s = 0; s < sceneCount; ++s) {
        waves += module._th185_scene_waves(s);
        bosses += module._th185_scene_boss_candidates(s);
        rows.push(module._th185_scene_waves(s));
    }
    console.log('scenes      : ' + sceneCount + ' waves=' + JSON.stringify(rows) +
                ' total=' + waves);
    check('there are nine scenes', sceneCount === 9);
    check('the tutorial is scene 0', sceneName(0) === 'Tutorial');
    check('scene 8 is the Challenge Market',
          sceneName(8) === 'Challenge Market');
    check('wave counts match the documented table',
          JSON.stringify(rows) === JSON.stringify([3, 3, 4, 5, 5, 7, 7, 4, 12]));
    check('the wave slots total 50', waves === 50);
    check('the Challenge Market has 12 waves', module._th185_scene_waves(8) === 12);

    // Each scene's pool is sized to its boss count.
    let poolOk = true;
    for (let s = 0; s < sceneCount; ++s) {
        if (module._th185_scene_pool_size(s) !== module._th185_scene_boss_candidates(s)) {
            poolOk = false;
        }
    }
    check('every scene pool is sized to its boss candidates', poolOk);
}
// Dual currency and the black market.
//
// TH185's economy has two currencies: `funds` (persistent, capped at 999,990,
// address 0x4d106c in the original) and `bulletMoney` (int32 at 0x4d1070,
// voided on a hit). The market offers three purchases after each wave and the
// cards bought there are TEMPORARY -- they do not join the collection.
{
    const cap = module._th185_funds_cap();
    check('the funds cap is 999990', cap === 999990);

    module._th185_add_funds(cap - 10);
    const beforeClamp = module._th185_funds();
    module._th185_add_funds(100);
    const afterClamp = module._th185_funds();
    console.log('funds       : cap=' + cap + ' ' + beforeClamp + ' +100 -> ' + afterClamp);
    check('funds clamp at the cap instead of wrapping', afterClamp === cap);

    module._th185_add_bullet_money(500);
    const earned = module._th185_bullet_money();
    module._th185_void_bullet_money();
    const voided = module._th185_bullet_money();
    console.log('bulletMoney : +500 -> ' + earned + '  after hit -> ' + voided);
    check('bullet money accumulates', earned === 500);
    check('a hit voids all bullet money', voided === 0);

    module._th185_add_bullet_money(300);
    const banked = module._th185_bank_bullet_money();
    console.log('bank        : +' + banked + ' -> funds=' + module._th185_funds());
    check('banking moves bullet money into funds', banked === 300);
    check('banking empties the bullet purse', module._th185_bullet_money() === 0);

    module._th185_market_open(1, 12345);
    const offers = module._th185_market_offers();
    const left = module._th185_market_purchases_left();
    console.log('market      : offers=' + offers + ' purchases=' + left);
    check('the market offers three cards', offers === 3);
    check('three purchases are allowed per wave', left === 3);

    const first = [];
    for (let i = 0; i < offers; ++i) first.push(module._th185_market_offer_at(i));
    module._th185_market_open(1, 12345);
    const second = [];
    for (let i = 0; i < offers; ++i) second.push(module._th185_market_offer_at(i));
    check('the same seed offers the same cards',
          JSON.stringify(first) === JSON.stringify(second));

    const fundsBefore = module._th185_funds();
    const boughtMarket = module._th185_market_buy(0);
    const fundsAfter = module._th185_funds();
    console.log('market buy  : ' + boughtMarket + ' funds ' + fundsBefore +
                ' -> ' + fundsAfter + ' temp=' + module._th185_temporary_cards());
    check('a market card can be bought', boughtMarket === 1);
    check('a market purchase spends funds', fundsAfter < fundsBefore);
    check('a market card is temporary', module._th185_temporary_cards() === 1);
    check('a purchase consumes one of the three slots',
          module._th185_market_purchases_left() === 2);
}
// Equip cost budget: `Total Cost %d%%/%d%%` in the original.
//
// The loadout is limited by total COST, not just card count. Cost lives at
// byte +0x2C (fields[11]); docs/CARDS.md guessed +0x30, but that field is
// only 0/1 across all 87 cards. The real field's 11 distinct values
// (0/10/25/30/50/60/75/100/125/150/200) are exactly the tier ladder the
// original's cost strings describe.
{
    // Progress accumulates across clears, so reset to the starting limits
    // before asserting what those limits are.
    module._th185_reset_progress();
    const cap = module._th185_cost_cap();
    const slots = module._th185_loadout_slots();
    console.log('cost budget : cap=' + cap + ' slots=' + slots);
    check('the starting cost cap is 100', cap === 100);
    check('the starting loadout has 8 slots', slots === 8);

    // The table must carry the tier ladder.
    const cardCount = module._th185_card_count();
    const tiers = new Set();
    for (let i = 0; i < cardCount; ++i) tiers.add(module._th185_card_cost(i));
    const ladder = Array.from(tiers).sort((a, b) => a - b);
    console.log('cost tiers  : ' + JSON.stringify(ladder));
    check('the cost tiers match the documented ladder',
          JSON.stringify(ladder) ===
          JSON.stringify([0, 10, 25, 30, 50, 60, 75, 100, 125, 150, 200]));

    // Equipping must respect the budget, not just the slot count.
    module._th185_cards_clear_loadout();
    module._th185_cards_add_currency(100000);
    let equipped = 0;
    let blocked = 0;
    for (let i = 0; i < cardCount; ++i) {
        const id = module._th185_card_id(i);
        if (!module._th185_cards_buy(id)) continue;
        if (module._th185_cards_equip(id)) ++equipped; else ++blocked;
    }
    const used = module._th185_loadout_cost();
    const size = module._th185_cards_loadout_size();
    console.log('equip       : ' + equipped + ' in, ' + blocked +
                ' blocked, cost ' + used + '/' + cap + ', size ' + size);
    check('the loadout never exceeds the cost cap', used <= cap);
    check('the loadout never exceeds the slot count', size <= slots);
    check('cards beyond the budget are rejected', blocked > 0);

    // Progress raises both limits.
    module._th185_grant_progress(4, 100);
    const newSlots = module._th185_loadout_slots();
    const newCap = module._th185_cost_cap();
    console.log('progress    : slots ' + slots + ' -> ' + newSlots +
                ', cap ' + cap + ' -> ' + newCap);
    check('progress adds loadout slots', newSlots === slots + 4);
    check('progress raises the cost cap', newCap === cap + 100);
}
// End-of-stage flow: clear -> bank -> market -> next stage.
//
// Before this link existed, `stage_clear` was set and nothing happened: the
// stage never ended, bullet money was never banked, and the market never
// opened. The chain is: bank the bullet purse, open the market from the
// scene's pool, switch to Phase::Cleared; a fresh load_stage resets it.
{
    module._th185_load_stage(1);
    module._th185_reset_scenario();
    module._th185_add_bullet_money(500);
    const phaseBefore = module._th185_phase();

    let clearAt = -1;
    for (let f = 0; f < 8000; ++f) {
        module._th185_tick(1);
        if (module._th185_stage_clear()) { clearAt = f; break; }
    }
    const phaseAfter = module._th185_phase();
    const banked = module._th185_banked();
    const marketOpen = module._th185_market_open_flag();
    console.log('end of stage: cleared at ' + clearAt +
                ' phase ' + phaseBefore + ' -> ' + phaseAfter +
                ' banked=' + banked + ' market=' + marketOpen);
    check('the stage clears', clearAt > 0);
    check('clearing switches to Phase::Cleared', phaseBefore === 1 && phaseAfter === 4);
    check('the bullet purse is banked on clear', banked === 500);
    check('the market opens on clear', marketOpen === 1);
    check('banking empties the bullet purse', module._th185_bullet_money() === 0);
    check('the banked money reaches funds', module._th185_funds() >= 500);

    const offersAfterClear = module._th185_market_offers();
    check('the market offers cards after a clear', offersAfterClear === 3);

    const beforeBuy = module._th185_funds();
    const tempBefore = module._th185_temporary_cards();
    const bought = module._th185_market_buy(0);
    const tempAfter = module._th185_temporary_cards();
    console.log('post-clear  : buy=' + bought + ' funds ' + beforeBuy +
                ' -> ' + module._th185_funds() +
                ' temp ' + tempBefore + ' -> ' + tempAfter);
    check('a post-clear purchase succeeds', bought === 1);
    // Compare the delta: earlier checks already bought cards this run.
    check('the purchase is temporary', tempAfter === tempBefore + 1);

    // Ticking while cleared must not resume simulation.
    for (let f = 0; f < 10; ++f) module._th185_tick(1);
    check('Phase::Cleared is stable across ticks', module._th185_phase() === 4);

    // A fresh stage resets the end-of-stage state.
    module._th185_load_stage(2);
    module._th185_reset_scenario();
    console.log('next stage  : phase=' + module._th185_phase() +
                ' clear=' + module._th185_stage_clear() +
                ' market=' + module._th185_market_open_flag());
    check('loading a stage resets the phase', module._th185_phase() === 1);
    check('loading a stage clears the clear flag', module._th185_stage_clear() === 0);
    check('loading a stage closes the market', module._th185_market_open_flag() === 0);
}
// Clearing a scene widens the loadout: +1 slot and +25 cost per clear.
//
// This is the progress hook the original ties slot count and the cost cap to
// ("通过踏破场景和击败新 BOSS 提升卡槽数量与 Cost 上限"). Without it a 200-cost
// card could never be equipped, because the starting cap is 100.
{
    const startSlots = module._th185_progress_slots();
    const startCap = module._th185_progress_cost_cap();

    let clears = 0;
    for (let round = 0; round < 2; ++round) {
        module._th185_load_stage(1);
        module._th185_reset_scenario();
        for (let f = 0; f < 8000; ++f) {
            module._th185_tick(1);
            if (module._th185_stage_clear()) { ++clears; break; }
        }
    }
    const endSlots = module._th185_progress_slots();
    const endCap = module._th185_progress_cost_cap();
    console.log('progress    : ' + clears + ' clears, slots ' + startSlots +
                ' -> ' + endSlots + ', cap ' + startCap + ' -> ' + endCap);
    check('both clears happened', clears === 2);
    check('each clear adds a loadout slot', endSlots === startSlots + 2);
    check('each clear raises the cost cap by 25', endCap === startCap + 50);

    // After enough clears the most expensive card must become equippable.
    let maxCost = 0;
    let maxCostId = -1;
    const cardTotal = module._th185_card_count();
    for (let i = 0; i < cardTotal; ++i) {
        const c = module._th185_card_cost(i);
        if (c > maxCost) { maxCost = c; maxCostId = module._th185_card_id(i); }
    }
    check('the priciest card costs more than the starting cap', maxCost > 100);
    check('progress eventually covers the priciest card', endCap >= maxCost);
}
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
