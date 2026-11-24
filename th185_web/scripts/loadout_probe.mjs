#!/usr/bin/env node
// Drive the loadout editor's data path against the real module.
//
// The shell's cards panel has no unit tests and no browser here, but the part
// that can actually be wrong is the QUERY LOGIC: which card key to use, whether
// the budget/slot gates agree with the module, and whether unequip removes the
// slot the user clicked. This script runs that logic against the real
// th185-app.mjs and prints one 'key: value' line per fact, which
// native/loadout_check.py then asserts on.
//
// It deliberately mirrors shell.mjs cardTableEntry()/loadoutSnapshot() rather
// than importing them: shell.mjs is a browser module (document/window at top
// level) and cannot be imported in Node.
//
// Usage: node scripts/loadout_probe.mjs <th185-app.mjs> <th185.dat>
// Exit codes: 0 ok; 1 unexpected state; 2 usage/environment missing

import { readFileSync, existsSync } from 'node:fs';
import { resolve, dirname } from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulePath, dataPath] = process.argv.slice(2);
if (!modulePath || !dataPath) {
  console.error('usage: node scripts/loadout_probe.mjs <th185-app.mjs> <th185.dat>');
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
  print: (line) => process.stdout.write(line + '\n'),
  printErr: (line) => process.stderr.write(line + '\n'),
  locateFile: (name) => resolve(dirname(moduleFile), name),
});

core.FS.writeFile('/th185.dat', new Uint8Array(readFileSync(data)), { canOwn: true });
if (!core._th185_initialize()) {
  console.error('FAIL initialize');
  process.exit(1);
}
// The exact browser sequence, including the reset_scenario the shell calls.
core._th185_load_stage(1);
core._th185_start();
core._th185_reset_scenario();

const NONE = 0xFFFFFFFF;
let failures = 0;

function entryFor(cardId) {
  const index = core._th185_card_index_of(cardId);
  if (index === NONE) return null;
  return {
    id: cardId,
    name: core.UTF8ToString(core._th185_card_name(index)),
    cost: core._th185_card_cost(index),
  };
}

function snapshot() {
  const equipped = [];
  for (let i = 0; i < core._th185_cards_loadout_size(); ++i) {
    equipped.push(core._th185_cards_loadout_at(i));
  }
  return {
    equipped,
    ownedCount: core._th185_cards_owned(),
    cost: core._th185_loadout_cost(),
    cap: core._th185_cost_cap(),
    slots: core._th185_loadout_slots(),
  };
}

console.log('card_count: ' + core._th185_card_count());
console.log('start_slots: ' + core._th185_loadout_slots());
console.log('start_cap: ' + core._th185_cost_cap());

// --- 1. index vs id are different keys -------------------------------
//
// Everything in the panel depends on this: name/cost take a table INDEX while
// equip/owns take a CARD ID. If they were interchangeable this would not matter
// and the conversion would be unnecessary.
let differing = 0;
let roundTripBad = 0;
for (let i = 0; i < core._th185_card_count(); ++i) {
  const id = core._th185_card_id(i);
  if (id !== i) ++differing;
  if (core._th185_card_index_of(id) !== i) ++roundTripBad;
}
console.log('rows_where_index_ne_id: ' + differing);
console.log('index_id_roundtrip_bad: ' + roundTripBad);
console.log('index_of_unknown: ' + core._th185_card_index_of(9999));

// --- 2. the opening panel renders the starter loadout ----------------
let snap = snapshot();
const initialNames = snap.equipped.map((id) => {
  const e = entryFor(id);
  return e ? e.name : '???';
});
console.log('start_equipped: ' + initialNames.join(','));
console.log('start_cost: ' + snap.cost);
console.log('start_owned_at_0: ' + core._th185_cards_owned_at(0));
console.log('start_owned_at_end: ' + core._th185_cards_owned_at(snap.ownedCount));
console.log('start_owns_first: ' + core._th185_cards_owns(snap.equipped[0]));

// --- 3. buy everything, then click every chip, like the panel does ---
core._th185_cards_add_currency(100000);
let bought = 0;
for (let i = 0; i < core._th185_card_count(); ++i) {
  const id = core._th185_card_id(i);
  if (core._th185_cards_buy(id)) ++bought;
}
console.log('bought: ' + bought);
console.log('owned_after_buy: ' + core._th185_cards_owned());

const ownedIds = [];
for (let i = 0; i < core._th185_cards_owned(); ++i) {
  const id = core._th185_cards_owned_at(i);
  if (id === NONE) break;
  ownedIds.push(id);
}
console.log('owned_ids_sorted: ' + (ownedIds.every((v, i) => i === 0 || ownedIds[i - 1] < v)));
console.log('owned_ids_all_owned: ' + ownedIds.every((id) => core._th185_cards_owns(id) === 1));

let equippedClicks = 0;
for (const id of ownedIds) {
  if (core._th185_cards_equip(id)) ++equippedClicks;
}
snap = snapshot();
console.log('equipped_after_click_all: ' + snap.equipped.length);
console.log('cost_after_click_all: ' + snap.cost);
console.log('cap_respected: ' + (snap.cost <= snap.cap));
console.log('slots_respected: ' + (snap.equipped.length <= snap.slots));

// --- 4. removal uses the SLOT INDEX, not the card id -----------------
const beforeCount = snap.equipped.length;
const removedId = snap.equipped[0];
const removedEntry = entryFor(removedId);
// Record the ids BEFORE and AFTER so the check can prove WHICH card went away.
// Asserting only "size dropped by 1" would pass even if the wrong card was
// removed -- and since slot 0 happens to hold a low id, passing a card id where
// a slot index belongs can silently delete a different card and still satisfy a
// count-based assertion. (Verified: that exact mistake passed the first
// version of this probe.)
const idsBefore = snap.equipped.slice();
const okUnequip = core._th185_cards_unequip(0);
const afterSnap = snapshot();
// The multiset difference: exactly one card must disappear, and it must be the
// one that was in slot 0.
const removedCounts = new Map();
for (const id of idsBefore) removedCounts.set(id, (removedCounts.get(id) || 0) + 1);
for (const id of afterSnap.equipped) removedCounts.set(id, (removedCounts.get(id) || 0) - 1);
const disappeared = [];
for (const [id, n] of removedCounts) { for (let k = 0; k < n; ++k) disappeared.push(id); }
console.log('unequip_slot0_was: ' + removedId);
console.log('unequip_disappeared: ' + (disappeared.length === 1 ? String(disappeared[0]) : disappeared.join(',')));
console.log('unequip_removed_slot0_card: ' + (disappeared.length === 1 && disappeared[0] === removedId));
console.log('unequip_returned: ' + okUnequip);
console.log('unequip_size_delta: ' + (afterSnap.equipped.length - beforeCount));
console.log('unequip_freed_cost: ' + (snap.cost - afterSnap.cost));
console.log('unequip_freed_expected: ' + (removedEntry ? removedEntry.cost : -1));
console.log('unequip_oob_rejected: ' + (core._th185_cards_unequip(99) === 0));

// --- 5. clear ---------------------------------------------------------
core._th185_cards_clear_loadout();
const cleared = snapshot();
console.log('cleared_size: ' + cleared.equipped.length);
console.log('cleared_cost: ' + cleared.cost);

// --- 6. the equipped set drives the run ------------------------------
//
// This is the point of the whole panel: what you equip has to change the run.
// EXTEND is one of the two cards CardManager::grants_life() recognises (the
// other is KOZUCHI), so equipping it must raise the starting life count.
// Measured ids are NOT the table indices: EXTEND is table index 9 but card id 3.
const EXTEND_ID = 3;
const extendCost = core._th185_card_cost(core._th185_card_index_of(EXTEND_ID));

core._th185_cards_clear_loadout();
core._th185_reset_scenario();
const livesWithout = core._th185_player_lives();

core._th185_cards_add_currency(1000);
const boughtExtend = core._th185_cards_buy(EXTEND_ID);
const equippedExtend = core._th185_cards_equip(EXTEND_ID);
core._th185_reset_scenario();
const livesWith = core._th185_player_lives();

console.log('extend_cost: ' + extendCost);
console.log('extend_bought: ' + boughtExtend);
console.log('extend_equipped: ' + equippedExtend);
console.log('lives_without: ' + livesWithout);
console.log('lives_with: ' + livesWith);
console.log('lives_delta: ' + (livesWith - livesWithout));

if (failures) process.exit(1);
