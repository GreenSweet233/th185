#!/usr/bin/env node
// TH185 目标二进制与资源指纹校验。
//
// 用途：
//   1. 校验 target.json 声明的原版文件（th185.exe / th185.dat / thbgm.dat / custom.exe）
//      的大小与 SHA-256 是否与本地实际文件一致。
//   2. 校验 th185.exe 的 PE 指纹（TimeDateStamp / .text SizeOfRawData / PE CheckSum），
//      这些字段由编译器与链接器写入，汉化组无法在不重编译的前提下改动。
//   3. 报告分节 SHA-256，作为后续"行为等价"验证的参照。
//
// 用法（从工作区根目录运行）：
//   .\th10_web\tools\node.exe th185_web/scripts/verify-target.mjs
//   .\th10_web\tools\node.exe th185_web/scripts/verify-target.mjs --game-dir "D:\\path\\to\\th185"
//
// 退出码：
//   0 = 全部通过
//   1 = 有文件哈希不匹配
//   2 = 文件缺失或不是有效 PE
//   3 = target.json 缺失或损坏

import {createHash} from 'node:crypto';
import {readFileSync, existsSync, statSync} from 'node:fs';
import {resolve, dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(here, '..');
const targetPath = resolve(webRoot, 'target.json');

if (!existsSync(targetPath)) {
  console.error('[ERR] 找不到 target.json: ' + targetPath);
  process.exit(3);
}

const target = JSON.parse(readFileSync(targetPath, 'utf8'));

// 游戏目录解析优先级：
//   1. --game-dir 命令行参数
//   2. TH185_GAME_DIR 环境变量
//   3. local.target.json（本机覆盖，已 gitignore；仓库约定是游戏目录与仓库同级）
//   4. target.json 的 executable 相对路径（仓库约定的标准布局）
function resolveGameDir() {
  const argIndex = process.argv.indexOf('--game-dir');
  if (argIndex > 0 && process.argv[argIndex + 1]) return resolve(process.argv[argIndex + 1]);
  if (process.env.TH185_GAME_DIR) return resolve(process.env.TH185_GAME_DIR);
  const localPath = resolve(webRoot, 'local.target.json');
  if (existsSync(localPath)) {
    try {
      const local = JSON.parse(readFileSync(localPath, 'utf8'));
      if (local.gameDir) return resolve(local.gameDir);
    } catch {
      console.warn('[WARN] local.target.json 解析失败，回退到仓库相对路径');
    }
  }
  return resolve(webRoot, dirname(target.executable));
}

const gameDir = resolveGameDir();

// 统一大写，避免与 target.json 里的大写哈希比较时误报
const sha256 = path => createHash('sha256').update(readFileSync(path)).digest('hex').toUpperCase();

// thprac 的 ID_TH185 识别条目 + 本机实测，三者必须同时成立
const EXPECTED_PE = {
  TimeDateStamp: 1659144319,
  TextRawSize: 711168,
  PECheckSum: 0x000DE79F,
};

function peChecksum(buf, peOffset) {
  // CheckSum 是 4 字节 DWORD，必须整体跳过（等价于先清零再算）。
  // 只跳过 2 字节会多累加高 16 位。
  const ckOff = peOffset + 24 + 64;
  let total = 0;
  for (let i = 0; i < buf.length; i += 2) {
    if (i >= ckOff && i < ckOff + 4) continue;
    const lo = buf[i];
    const hi = i + 1 < buf.length ? buf[i + 1] : 0;
    total += lo + (hi << 8);
    total = (total & 0xffff) + (total >> 16);
  }
  total = (total & 0xffff) + (total >> 16);
  total += buf.length;
  return total >>> 0;
}

function parsePE(buf) {
  if (buf.length < 0x200 || buf[0] !== 0x4d || buf[1] !== 0x5a) return null;
  const peOffset = buf.readUInt32LE(0x3c);
  if (buf.readUInt32LE(peOffset) !== 0x4550) return null;
  const numSections = buf.readUInt16LE(peOffset + 6);
  const timeStamp = buf.readUInt32LE(peOffset + 8);
  const optSize = buf.readUInt16LE(peOffset + 20);
  const optOff = peOffset + 24;
  const imageBase = buf.readUInt32LE(optOff + 28);
  const entry = buf.readUInt32LE(optOff + 16);
  const checksum = buf.readUInt32LE(optOff + 64);
  const secOff = optOff + optSize;
  const sections = [];
  for (let i = 0; i < numSections; i++) {
    const o = secOff + i * 40;
    const name = buf.toString('ascii', o, o + 8).replace(/\0+$/, '');
    sections.push({
      name,
      virtualSize: buf.readUInt32LE(o + 8),
      virtualAddress: buf.readUInt32LE(o + 12),
      rawSize: buf.readUInt32LE(o + 16),
      rawOffset: buf.readUInt32LE(o + 20),
    });
  }
  return {peOffset, numSections, timeStamp, imageBase, entry, checksum, sections};
}

console.log('目标版本 : ' + target.game + ' v' + target.version + ' (' + target.language + ')');
console.log('游戏目录 : ' + gameDir);
console.log('='.repeat(78));

const files = [
  ['th185.exe', target.sha256, 'executable'],
  ['th185.dat', target.data.sha256, 'data'],
  ['thbgm.dat', target.music.sha256, 'music'],
  ['custom.exe', target.config.sha256, 'config'],
];

const missing = [];
const mismatched = [];

for (const [name, expected, role] of files) {
  const path = join(gameDir, name);
  if (!existsSync(path)) {
    missing.push(name);
    console.log('  缺失   ' + name + ' (' + role + ')');
    continue;
  }
  const actual = sha256(path);
  const size = statSync(path).size;
  if (actual === expected) {
    console.log('  OK     ' + name.padEnd(14) + String(size).padStart(10) + '  SHA256=' + actual.slice(0, 16) + '...');
  } else {
    mismatched.push(name);
    console.log('  不匹配 ' + name.padEnd(14) + String(size).padStart(10) + '  SHA256=' + actual);
    console.log('         ' + ' '.repeat(14) + ' '.repeat(10) + '  期望 SHA256=' + expected);
  }
}

console.log('='.repeat(78));

let peOk = false;
const exePath = join(gameDir, 'th185.exe');
if (existsSync(exePath)) {
  const buf = readFileSync(exePath);
  const pe = parsePE(buf);
  if (!pe) {
    console.log('[ERR] th185.exe 不是有效的 PE32 文件');
    process.exit(2);
  }

  const textSection = pe.sections.find(s => s.name === '.text');
  const actual = {
    TimeDateStamp: pe.timeStamp,
    TextRawSize: textSection ? textSection.rawSize : null,
    PECheckSum: pe.checksum,
  };

  console.log('PE 指纹（thprac ID_TH185 识别 + 重建基准）:');
  for (const [key, expected] of Object.entries(EXPECTED_PE)) {
    const got = actual[key];
    const mark = got === expected ? 'OK' : 'MISMATCH';
    console.log('  ' + key.padEnd(16) + ' 实测=' + got + '  期望=' + expected + '  ' + mark);
  }

  const computed = peChecksum(buf, pe.peOffset);
  const selfConsistent = computed === pe.checksum;
  console.log('  ' + 'CheckSum 自洽'.padEnd(14) + ' 重算=0x' + computed.toString(16).toUpperCase().padStart(8, '0')
    + '  存储=0x' + pe.checksum.toString(16).toUpperCase().padStart(8, '0')
    + '  ' + (selfConsistent ? 'OK' : 'MISMATCH（文件被修改过）'));

  peOk = Object.entries(EXPECTED_PE).every(([k, v]) => actual[k] === v) && selfConsistent;

  console.log('='.repeat(78));
  console.log('分节 SHA-256（行为等价验证的参照，不是字节级 matching 门禁）:');
  for (const s of pe.sections) {
    const sec = buf.subarray(s.rawOffset, s.rawOffset + s.rawSize);
    console.log('  ' + s.name.padEnd(10)
      + ' VA=0x' + s.virtualAddress.toString(16).padStart(8, '0')
      + ' RawSize=0x' + s.rawSize.toString(16).padStart(6, '0')
      + '  SHA256=' + createHash('sha256').update(sec).digest('hex').toUpperCase());
  }
  const maxEnd = Math.max(...pe.sections.map(s => s.rawOffset + s.rawSize));
  console.log('\n  overlay = ' + (buf.length - maxEnd) + ' 字节（0 表示无壳、无附加数据）');
  console.log('  ImageBase = 0x' + pe.imageBase.toString(16).toUpperCase().padStart(8, '0')
    + '  EntryPoint RVA = 0x' + pe.entry.toString(16).toUpperCase().padStart(8, '0'));
  console.log('='.repeat(78));
}

if (missing.length) {
  console.log('结果：FAIL —— ' + missing.length + ' 个文件缺失: ' + missing.join(', '));
  process.exit(2);
}
if (mismatched.length) {
  console.log('结果：FAIL —— ' + mismatched.length + ' 个文件哈希不匹配: ' + mismatched.join(', '));
  console.log('      这通常意味着该目录是汉化版 / 改版 / 或非 v1.00a 版本。');
  process.exit(1);
}
if (!peOk) {
  console.log('结果：FAIL —— PE 指纹不匹配');
  process.exit(2);
}

console.log('结果：PASS —— 4 个文件全部匹配原版日文 v1.00a，PE 指纹与 CheckSum 自洽');
process.exit(0);
