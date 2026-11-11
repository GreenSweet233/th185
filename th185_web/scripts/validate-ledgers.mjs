#!/usr/bin/env node
// TH185 还原账本校验。
//
// 与 th095 的 matching 账本不同：TH185 走「功能还原重建」路线，没有字节级
// 匹配，所以账本按「子系统 + 可观察行为」组织，而不是按函数地址。
//
// 校验内容：
//   1. subsystems.csv   —— 每个子系统的 status / evidence_level 取值合法，
//                          parent 与 deps 必须指向存在的子系统，且无环。
//   2. behaviors.csv    —— 每条断言必须属于一个存在的子系统，
//                          status=verified 时必须给出 test_ref。
//   3. symbols.csv      —— 地址规范、无重复、confidence 合法。
//   4. target.json      —— 必须存在且关键字段齐全。
//
// 用法（从仓库根目录）：
//   node th185_web/scripts/validate-ledgers.mjs
//   node th185_web/scripts/validate-ledgers.mjs --json
//
// 退出码：
//   0 = 全部通过
//   1 = 校验失败
//   2 = 账本文件缺失

import {readFileSync, existsSync} from 'node:fs';
import {resolve, dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(here, "..");
const configDir = join(webRoot, "config");

const SUBSYSTEM_STATUS = new Set([
  "not-started", "in-progress", "partial", "complete", "blocked", "excluded",
]);
const EVIDENCE_LEVELS = new Set([
  "none",            // 无证据
  "inferred",        // 推断，可被推翻
  "corroborated",    // 有外部或相邻来源支持
  "observed",        // 直接从原版运行时/内存读出
  "behavioral",      // 行为等价已通过比对
]);
const BEHAVIOR_STATUS = new Set([
  "planned", "implemented", "verified", "waived",
]);
const CONFIDENCE = new Set([
  "inferred", "corroborated", "observed",
]);

const problems = [];
const stats = {};

function fail(message) {
  problems.push(message);
}

/** 极简 CSV 解析：支持双引号包裹与 "" 转义，足够处理本项目账本。 */
function parseCsv(text) {
  const rows = [];
  let row = [];
  let field = "";
  let inQuotes = false;
  for (let i = 0; i < text.length; i++) {
    const ch = text[i];
    if (inQuotes) {
      if (ch === '"') {
        if (text[i + 1] === '"') { field += '"'; i++; }
        else inQuotes = false;
      } else field += ch;
      continue;
    }
    if (ch === '"') { inQuotes = true; continue; }
    if (ch === ",") { row.push(field); field = ""; continue; }
    if (ch === "\r") continue;
    if (ch === "\n") { row.push(field); rows.push(row); row = []; field = ""; continue; }
    field += ch;
  }
  if (field.length || row.length) { row.push(field); rows.push(row); }
  return rows.filter((r) => r.length && !(r.length === 1 && r[0] === ""));
}

function loadCsv(name, requiredFields) {
  const path = join(configDir, name);
  if (!existsSync(path)) {
    fail(`${name}: 文件不存在（${path}）`);
    return null;
  }
  const rows = parseCsv(readFileSync(path, "utf8"));
  if (!rows.length) {
    fail(`${name}: 空文件，缺少表头`);
    return null;
  }
  const header = rows[0];
  for (const field of requiredFields) {
    if (!header.includes(field)) fail(`${name}: 表头缺少字段 ${field}`);
  }
  const index = Object.fromEntries(header.map((h, i) => [h, i]));
  const records = [];
  for (let r = 1; r < rows.length; r++) {
    const cells = rows[r];
    if (cells.length !== header.length) {
      fail(`${name}:${r + 1}: 列数 ${cells.length} 与表头 ${header.length} 不符`);
      continue;
    }
    const record = {};
    for (const [key, i] of Object.entries(index)) record[key] = cells[i];
    record.__line = r + 1;
    records.push(record);
  }
  return records;
}

function checkAddress(raw, label) {
  if (!/^0x[0-9A-F]{8}$/.test(raw)) {
    fail(`${label}: 地址必须是 0x + 8 位大写十六进制（实得 ${raw}）`);
    return null;
  }
  return parseInt(raw, 16);
}

// ---------- target.json ----------
const targetPath = join(webRoot, "target.json");
if (!existsSync(targetPath)) {
  fail("target.json: 文件不存在");
} else {
  let target;
  try {
    target = JSON.parse(readFileSync(targetPath, "utf8"));
  } catch (error) {
    fail(`target.json: JSON 解析失败（${error.message}）`);
  }
  if (target) {
    for (const key of ["game", "version", "sha256"]) {
      if (!target[key]) fail(`target.json: 缺少字段 ${key}`);
    }
    if (target.sha256 && !/^[0-9A-F]{64}$/.test(target.sha256)) {
      fail("target.json: sha256 必须是 64 位大写十六进制");
    }
    stats.target = target.game + " v" + target.version;
  }
}

// ---------- subsystems.csv ----------
const subsystems = loadCsv("subsystems.csv", [
  "id", "title", "parent", "deps", "status", "evidence_level", "owner", "notes",
]);
const subsystemIds = new Map();
if (subsystems) {
  for (const row of subsystems) {
    const where = `subsystems.csv:${row.__line}`;
    if (!row.id) { fail(`${where}: id 为空`); continue; }
    if (subsystemIds.has(row.id)) { fail(`${where}: id 重复（${row.id}）`); continue; }
    if (!row.title) fail(`${where}: title 为空`);
    if (!SUBSYSTEM_STATUS.has(row.status)) {
      fail(`${where}: status 非法（${row.status}），允许：${[...SUBSYSTEM_STATUS].join(" / ")}`);
    }
    if (!EVIDENCE_LEVELS.has(row.evidence_level)) {
      fail(`${where}: evidence_level 非法（${row.evidence_level}），允许：${[...EVIDENCE_LEVELS].join(" / ")}`);
    }
    subsystemIds.set(row.id, row);
  }
  // parent / deps 必须指向存在的子系统
  for (const row of subsystems) {
    const where = `subsystems.csv:${row.__line}`;
    if (row.parent && !subsystemIds.has(row.parent)) {
      fail(`${where}: parent 指向不存在的子系统（${row.parent}）`);
    }
    if (row.deps) {
      for (const dep of row.deps.split(";").map((s) => s.trim()).filter(Boolean)) {
        if (!subsystemIds.has(dep)) {
          fail(`${where}: deps 指向不存在的子系统（${dep}）`);
        }
        if (dep === row.id) fail(`${where}: deps 不能引用自身`);
      }
    }
  }
  // 环检测（沿 deps + parent）
  const WHITE = 0, GRAY = 1, BLACK = 2;
  const color = new Map([...subsystemIds.keys()].map((id) => [id, WHITE]));
  const edges = (id) => {
    const row = subsystemIds.get(id);
    const out = [];
    if (row.parent) out.push(row.parent);
    if (row.deps) out.push(...row.deps.split(";").map((s) => s.trim()).filter(Boolean));
    return out;
  };
  const visit = (id, path) => {
    if (color.get(id) === GRAY) {
      fail(`subsystems.csv: 依赖环 ${[...path, id].join(" -> ")}`);
      return;
    }
    if (color.get(id) === BLACK) return;
    color.set(id, GRAY);
    for (const next of edges(id)) {
      if (subsystemIds.has(next)) visit(next, [...path, id]);
    }
    color.set(id, BLACK);
  };
  for (const id of subsystemIds.keys()) visit(id, []);

  const byStatus = {};
  for (const row of subsystems) byStatus[row.status] = (byStatus[row.status] || 0) + 1;
  stats.subsystems = { total: subsystems.length, byStatus };
}

// ---------- behaviors.csv ----------
const behaviors = loadCsv("behaviors.csv", [
  "id", "subsystem", "assertion", "source", "evidence_level", "status", "test_ref", "notes",
]);
if (behaviors) {
  const seen = new Set();
  for (const row of behaviors) {
    const where = `behaviors.csv:${row.__line}`;
    if (!row.id) { fail(`${where}: id 为空`); continue; }
    if (seen.has(row.id)) { fail(`${where}: id 重复（${row.id}）`); continue; }
    seen.add(row.id);
    if (!subsystemIds.has(row.subsystem)) {
      fail(`${where}: subsystem 不存在（${row.subsystem}）`);
    }
    if (!row.assertion) fail(`${where}: assertion 为空`);
    if (!BEHAVIOR_STATUS.has(row.status)) {
      fail(`${where}: status 非法（${row.status}）`);
    }
    if (!EVIDENCE_LEVELS.has(row.evidence_level)) {
      fail(`${where}: evidence_level 非法（${row.evidence_level}）`);
    }
    if (row.status === "verified") {
      if (!row.test_ref) fail(`${where}: status=verified 必须给出 test_ref`);
      if (row.evidence_level !== "behavioral" && row.evidence_level !== "observed") {
        fail(`${where}: status=verified 要求 evidence_level 为 behavioral 或 observed`);
      }
    }
    if (row.status === "waived" && !row.notes) {
      fail(`${where}: status=waived 必须在 notes 说明理由`);
    }
  }
  const byStatus = {};
  for (const row of behaviors) byStatus[row.status] = (byStatus[row.status] || 0) + 1;
  stats.behaviors = { total: behaviors.length, byStatus };
}

// ---------- symbols.csv ----------
const symbols = loadCsv("symbols.csv", [
  "address", "kind", "name", "source", "confidence", "evidence",
]);
if (symbols) {
  const seenAddress = new Set();
  const seenName = new Set();
  for (const row of symbols) {
    const where = `symbols.csv:${row.__line}`;
    const value = checkAddress(row.address, where);
    if (value !== null) {
      if (seenAddress.has(value)) fail(`${where}: 地址重复（${row.address}）`);
      seenAddress.add(value);
    }
    if (!row.name) fail(`${where}: name 为空`);
    else if (seenName.has(row.name)) fail(`${where}: name 重复（${row.name}）`);
    else seenName.add(row.name);
    if (!row.kind) fail(`${where}: kind 为空`);
    if (!CONFIDENCE.has(row.confidence)) {
      fail(`${where}: confidence 非法（${row.confidence}），允许：${[...CONFIDENCE].join(" / ")}`);
    }
    if (!row.evidence) fail(`${where}: evidence 为空（必须写明来源）`);
  }
  stats.symbols = symbols.length;
}

// ---------- 输出 ----------
const asJson = process.argv.includes("--json");
if (asJson) {
  console.log(JSON.stringify({ok: problems.length === 0, problems, stats}, null, 2));
} else {
  if (stats.target) console.log(`目标     : ${stats.target}`);
  if (stats.subsystems) {
    const parts = Object.entries(stats.subsystems.byStatus)
      .map(([k, v]) => `${k}=${v}`).join("  ");
    console.log(`子系统   : ${stats.subsystems.total} 个  (${parts})`);
  }
  if (stats.behaviors) {
    const parts = Object.entries(stats.behaviors.byStatus)
      .map(([k, v]) => `${k}=${v}`).join("  ");
    console.log(`行为断言 : ${stats.behaviors.total} 条  (${parts || "空"})`);
  }
  if (stats.symbols !== undefined) console.log(`已知符号 : ${stats.symbols} 条`);
  console.log("=".repeat(70));
  if (problems.length) {
    console.log(`结果：FAIL —— ${problems.length} 个问题`);
    for (const problem of problems) console.log("  " + problem);
  } else {
    console.log("结果：PASS —— 账本一致");
  }
}
process.exit(problems.length ? 1 : 0);
