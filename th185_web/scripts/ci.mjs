#!/usr/bin/env node
// TH185 还原工作流的统一入口。
//
// 串联全部 fail-closed 检查。与 th095 的 ci.py 同源思路：
// 即使前面的检查失败，也要把后面的都跑完，一次给出完整画面。
//
// 检查项：
//   1. target    —— 原版文件哈希 + PE 指纹 + CheckSum 自洽
//   2. ledgers   —— 子系统 / 行为 / 符号账本一致性
//   3. progress  —— 进度快照可生成（不写文件，只验证）
//
// 用法：
//   node th185_web/scripts/ci.mjs
//   node th185_web/scripts/ci.mjs --skip-target
//
// 退出码：0 = 全部通过；1 = 有检查失败

import {spawnSync} from 'node:child_process';
import {resolve, dirname} from 'node:path';
import {fileURLToPath} from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(here, "..");
const repoRoot = resolve(webRoot, "..");

const skipTarget = process.argv.includes("--skip-target");

const checks = [];
if (!skipTarget) {
  checks.push({
    name: "target",
    args: [resolve(here, "verify-target.mjs")],
  });
}
checks.push(
  { name: "ledgers", args: [resolve(here, "validate-ledgers.mjs")] },
  { name: "progress", args: [resolve(here, "progress.mjs"), "--stdout"] },
);

let failures = 0;
const width = Math.max(...checks.map((c) => c.name.length));

for (const check of checks) {
  const result = spawnSync(process.execPath, check.args, {
    cwd: repoRoot,
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
  });
  const ok = result.status === 0;
  const label = ok ? "PASS" : "FAIL";
  console.log(`[${label}] ${check.name.padEnd(width)}  (${check.args.slice(1).join(" ") || "default"})`);

  // progress --stdout 正常时会输出整篇 markdown，太长，只保留摘要
  let output = ((result.stdout || "") + (result.stderr || "")).trim();
  if (check.name === "progress" && ok) {
    const summary = output.split("\n").filter((line) =>
      line.startsWith("| 子系统总数") ||
      line.startsWith("| 已完成") ||
      line.startsWith("| 行为断言总数") ||
      line.startsWith("| 已验证断言"));
    output = summary.join("\n");
  }
  if (output) {
    for (const line of output.split("\n")) console.log("         " + line);
  }
  if (!ok) failures++;
}

console.log("");
if (failures) {
  console.log(failures + " 项检查失败");
  process.exit(1);
}
console.log("全部检查通过");
if (skipTarget) {
  console.log("注意：跳过了 target 检查。声称任何行为已验证前必须不带 --skip-target 重跑。");
}
