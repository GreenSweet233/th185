#!/usr/bin/env node
// 生成 TH185 还原进度快照。
//
// 与 th095 的 progress.py 同源思路：只统计账本里明确记录的事实，
// 不做任何外推。一个子系统标 complete 不代表它的行为断言全部 verified，
// 所以两个维度分开报告。
//
// 用法：
//   node th185_web/scripts/progress.mjs                     # write docs/PROGRESS.md
//   node th185_web/scripts/progress.mjs --stdout            # print, do not write
//   node th185_web/scripts/progress.mjs --output <path>     # write elsewhere
//   node th185_web/scripts/progress.mjs --check             # fail if the
//                                                           # committed file is stale
//
// Exit codes: 0 ok / up to date; 1 stale or missing (only with --check).

import {readFileSync, writeFileSync, existsSync, mkdirSync} from 'node:fs';
import {resolve, dirname, join} from 'node:path';
import {fileURLToPath} from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(here, "..");
const repoRoot = resolve(webRoot, "..");
const configDir = join(webRoot, "config");

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

function loadCsv(name) {
  const path = join(configDir, name);
  if (!existsSync(path)) return [];
  const rows = parseCsv(readFileSync(path, "utf8"));
  if (!rows.length) return [];
  const header = rows[0];
  return rows.slice(1).map((cells) => {
    const record = {};
    header.forEach((key, i) => { record[key] = cells[i] ?? ""; });
    return record;
  });
}

const subsystems = loadCsv("subsystems.csv");
const behaviors = loadCsv("behaviors.csv");
const symbols = loadCsv("symbols.csv");
const target = JSON.parse(readFileSync(join(webRoot, "target.json"), "utf8"));

const STATUS_ORDER = ["complete", "partial", "in-progress", "blocked", "not-started", "excluded"];
const STATUS_LABEL = {
  complete: "已完成",
  partial: "部分完成",
  "in-progress": "进行中",
  blocked: "阻塞",
  "not-started": "未开始",
  excluded: "已排除",
};

const byStatus = {};
for (const status of STATUS_ORDER) byStatus[status] = [];
for (const row of subsystems) {
  (byStatus[row.status] ||= []).push(row);
}

const behaviorByStatus = {};
for (const row of behaviors) (behaviorByStatus[row.status] ||= []).push(row);

const total = subsystems.length;
const complete = (byStatus.complete || []).length;
const blocked = (byStatus.blocked || []).length;
const verified = (behaviorByStatus.verified || []).length;
const pct = total ? (100 * complete) / total : 0;

const lines = [];
lines.push("# TH185 还原进度");
lines.push("");
lines.push("> 由 `node th185_web/scripts/progress.mjs` 自动生成，**不要手改**。");
lines.push("");
lines.push("目标版本：`" + target.game + "` v" + target.version + "（" + target.language + "）");
lines.push("");
lines.push("## 总览");
lines.push("");
lines.push("| 指标 | 数量 |");
lines.push("| --- | ---: |");
lines.push("| 子系统总数 | " + total + " |");
lines.push("| 已完成 | " + complete + " |");
lines.push("| 阻塞 | " + blocked + " |");
lines.push("| 行为断言总数 | " + behaviors.length + " |");
lines.push("| 已验证断言 | " + verified + " |");
lines.push("| 已知符号 | " + symbols.length + " |");
lines.push("| 子系统完成率 | " + pct.toFixed(1) + "% |");
lines.push("");
lines.push("## 子系统状态");
lines.push("");
lines.push("| 状态 | 数量 | 子系统 |");
lines.push("| --- | ---: | --- |");
for (const status of STATUS_ORDER) {
  const rows = byStatus[status] || [];
  if (!rows.length) continue;
  const names = rows.map((r) => r.id).join(" / ");
  lines.push("| " + STATUS_LABEL[status] + " | " + rows.length + " | " + names + " |");
}
lines.push("");
lines.push("## 阻塞项");
lines.push("");
const blockedRows = byStatus.blocked || [];
if (!blockedRows.length) {
  lines.push("（无）");
} else {
  lines.push("| 子系统 | 说明 |");
  lines.push("| --- | --- |");
  for (const row of blockedRows) lines.push("| " + row.id + " | " + row.notes + " |");
}
lines.push("");
lines.push("## 这些数字不代表什么");
lines.push("");
lines.push("- 子系统标 `complete` **不等于**它的行为全部与原版一致——行为断言是独立的账本。");
lines.push("- 有源码 **不等于**行为正确。");
lines.push("- `verified` 断言必须有 `test_ref` 指向可复现的测试，否则校验器会拒绝。");
lines.push("- TH185 **没有 Replay**，所以不存在「录像逐帧比对」这种强证据；");
lines.push("  验证依赖内存状态快照与 Force Wave 确定性注入，强度低于 th11 的录像比对。");
lines.push("");

const text = lines.join("\n");
const args = process.argv.slice(2);
if (args.includes("--stdout")) {
  process.stdout.write(text);
} else if (args.includes("--check")) {
  // Fail-closed freshness check.
  //
  // The committed docs/PROGRESS.md had drifted 155 commits behind the ledgers
  // (it still said "2 complete / 87 behaviors" while the CSV said
  // "6 complete / 370 behaviors"). Nothing caught it, because CI only ran
  // "--stdout", which proves the generator works but not that its output was
  // ever committed. A stale snapshot is worse than no snapshot: it is a
  // document that looks authoritative and is wrong.
  const committed = join(webRoot, "docs", "PROGRESS.md");
  if (!existsSync(committed)) {
    console.error("error: docs/PROGRESS.md is missing; run 'node th185_web/scripts/progress.mjs'");
    process.exit(1);
  }
  const actual = readFileSync(committed, "utf8").replace(/\r\n/g, "\n");
  const expected = text.replace(/\r\n/g, "\n");
  if (actual !== expected) {
    const a = actual.split("\n");
    const b = expected.split("\n");
    const width = Math.max(a.length, b.length);
    let shown = 0;
    console.error("docs/PROGRESS.md is stale (differs from the ledgers).");
    console.error("Regenerate with: node th185_web/scripts/progress.mjs");
    console.error("");
    for (let i = 0; i < width && shown < 12; ++i) {
      if (a[i] !== b[i]) {
        console.error("  line " + (i + 1));
        console.error("    committed  : " + (a[i] ?? "(missing)"));
        console.error("    regenerated: " + (b[i] ?? "(missing)"));
        ++shown;
      }
    }
    if (shown === 12) console.error("  ... (more differences not shown)");
    process.exit(1);
  }
  console.log("docs/PROGRESS.md is up to date (" + expected.length + " bytes)");
} else {
  const index = args.indexOf("--output");
  const output = index >= 0 && args[index + 1]
    ? resolve(process.cwd(), args[index + 1])
    : join(webRoot, "docs", "PROGRESS.md");
  mkdirSync(dirname(output), { recursive: true });
  writeFileSync(output, text, "utf8");
  console.log("wrote " + output);
}
