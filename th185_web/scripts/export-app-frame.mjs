#!/usr/bin/env node
// Run the application module and export a frame it rendered.
//
// Unlike scripts/export-frame.mjs (which drives the harness), this exercises
// the SAME module the web shell loads, so a frame here is a frame the browser
// would show.

import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { resolve, dirname, basename } from 'node:path';
import { pathToFileURL } from 'node:url';

const [modulePath, dataPath, outputPath] = process.argv.slice(2);
if (!modulePath || !dataPath || !outputPath) {
  console.error('usage: node scripts/export-app-frame.mjs <th185-app.mjs> <th185.dat> <out.png>');
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

const archive = readFileSync(data);
module.FS.writeFile('/th185.dat', new Uint8Array(archive), { canOwn: true });

if (!module._th185_initialize()) {
  console.error('FAIL initialize: ' + module.UTF8ToString(module._th185_error()));
  process.exit(1);
}
if (!module._th185_start()) {
  console.error('FAIL start: ' + module.UTF8ToString(module._th185_error()));
  process.exit(1);
}

// Set up a scenario that shows all three systems at once: targets, bullets in
// flight, and the player. reset_scenario first because initialise already
// spawned a wave that a long run would have destroyed.
const SHOOT = 1 << 0;
// No spawn_test_wave: the enemies in this frame come from the ECL scene
// controller's op1013 schedules, which is the real data path.
module._th185_reset_scenario();
// Long enough for the enemies to return fire, so the frame shows both sides.
// Long enough for the ECL schedules to spawn several waves.
for (let i = 0; i < 600; ++i) {
  const held = i >= 20 ? SHOOT : 0;
  if (!module._th185_tick(held)) {
    console.error('FAIL tick: ' + module.UTF8ToString(module._th185_error()));
    process.exit(1);
  }
}

const width = module._th185_frame_width();
const height = module._th185_frame_height();
const bytes = module._th185_frame_bytes();
const quads = module._th185_render_quads();
const live = module._th185_live_animations();
console.log('frame      : ' + width + 'x' + height);
console.log('bytes      : ' + bytes);
console.log('quads      : ' + quads);
console.log('animations : ' + live);
console.log('bullets    : ' + module._th185_bullets_alive());
console.log('enemies    : ' + module._th185_enemies_alive());
console.log('schedules  : ' + module._th185_schedules_total() + ' fired ' + module._th185_schedules_fired());
console.log('templates  : ' + module._th185_wave_templates() + ' last ' + module._th185_last_wave_template());
console.log('hits       : ' + module._th185_collision_hits());
console.log('enemyShots : ' + module._th185_enemy_shots());
console.log('lives      : ' + module._th185_player_lives());

if (!width || !height || !bytes) {
  console.error('FAIL: the application produced no framebuffer');
  process.exit(1);
}

const pointer = module._th185_frame_pixels();
const rgba = Buffer.from(module.HEAPU8.subarray(pointer, pointer + bytes));

function crc32(buffer) {
  let crc = 0xffffffff;
  for (let i = 0; i < buffer.length; ++i) {
    crc ^= buffer[i];
    for (let bit = 0; bit < 8; ++bit) {
      crc = (crc & 1) ? (0xedb88320 ^ (crc >>> 1)) : (crc >>> 1);
    }
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function chunk(tag, payload) {
  const length = Buffer.alloc(4);
  length.writeUInt32BE(payload.length, 0);
  const body = Buffer.concat([Buffer.from(tag, 'ascii'), payload]);
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(body), 0);
  return Buffer.concat([length, body, crc]);
}

const raw = Buffer.alloc(height * (1 + width * 4));
for (let y = 0; y < height; ++y) {
  raw[y * (1 + width * 4)] = 0;
  rgba.copy(raw, y * (1 + width * 4) + 1, y * width * 4, (y + 1) * width * 4);
}

const zlib = [Buffer.from([0x78, 0x01])];
const BLOCK = 65535;
for (let at = 0; at < raw.length; at += BLOCK) {
  const count = Math.min(BLOCK, raw.length - at);
  const last = at + count >= raw.length ? 1 : 0;
  const header = Buffer.alloc(5);
  header[0] = last;
  header.writeUInt16LE(count, 1);
  header.writeUInt16LE(~count & 0xffff, 3);
  zlib.push(header, raw.subarray(at, at + count));
}
let a = 1, b = 0;
for (let i = 0; i < raw.length; ++i) {
  a = (a + raw[i]) % 65521;
  b = (b + a) % 65521;
}
const adler = Buffer.alloc(4);
adler.writeUInt32BE(((b << 16) | a) >>> 0, 0);
zlib.push(adler);

const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(width, 0);
ihdr.writeUInt32BE(height, 4);
ihdr[8] = 8;
ihdr[9] = 6;
const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
  chunk('IHDR', ihdr),
  chunk('IDAT', Buffer.concat(zlib)),
  chunk('IEND', Buffer.alloc(0)),
]);

mkdirSync(dirname(resolve(outputPath)), { recursive: true });
writeFileSync(outputPath, png);
console.log('wrote ' + outputPath + ' (' + png.length + ' bytes)');
