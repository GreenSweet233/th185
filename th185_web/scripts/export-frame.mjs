#!/usr/bin/env node
// Render a real ANM scene and write it as a PNG.
//
// The harness hands its framebuffer out through WASM memory (see
// HarnessGame.cpp) because NODEFS writes do not reliably reach the host before
// the process exits. This script reads that memory and writes the PNG with
// Node's synchronous file IO.

import { readFileSync, writeFileSync, existsSync, mkdirSync } from 'node:fs';
import { resolve, dirname, basename } from 'node:path';
import { pathToFileURL } from 'node:url';

const [harnessPath, dataPath, outputPath] = process.argv.slice(2);
if (!harnessPath || !dataPath || !outputPath) {
  console.error('usage: node scripts/export-frame.mjs <harness.cjs> <th185.dat> <out.png>');
  process.exit(2);
}

const harness = resolve(harnessPath);
const data = resolve(dataPath);
if (!existsSync(harness) || !existsSync(data)) {
  console.error('error: missing harness or data file');
  process.exit(2);
}

const factory = (await import(pathToFileURL(harness).href)).default;
const module = await factory({
  print: (line) => process.stdout.write(line + '\n'),
  printErr: (line) => process.stderr.write(line + '\n'),
  locateFile: (name) => resolve(dirname(harness), name),
});

try { module.FS.mkdir('/data'); } catch {}
module.FS.mount(module.NODEFS, { root: dirname(data) }, '/data');
module.callMain(['/data/' + basename(data)]);

const width = module._th185_frame_width();
const height = module._th185_frame_height();
const bytes = module._th185_frame_bytes();
if (!width || !height || !bytes) {
  console.error('error: the harness did not produce a frame');
  process.exit(1);
}
const pointer = module._th185_frame_pixels();
const rgba = Buffer.from(module.HEAPU8.subarray(pointer, pointer + bytes));

// --- Minimal PNG encoder (stored deflate blocks) ---------------------------
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

// Scanlines with filter byte 0.
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
ihdr[8] = 8;   // bit depth
ihdr[9] = 6;   // RGBA
const png = Buffer.concat([
  Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
  chunk('IHDR', ihdr),
  chunk('IDAT', Buffer.concat(zlib)),
  chunk('IEND', Buffer.alloc(0)),
]);

mkdirSync(dirname(resolve(outputPath)), { recursive: true });
writeFileSync(outputPath, png);
console.log('wrote ' + outputPath + ' (' + png.length + ' bytes, ' + width + 'x' + height + ')');
