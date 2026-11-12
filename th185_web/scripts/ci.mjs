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
  // 探针自检需要 unicorn + capstone。缺依赖时跳过而不是判失败，
  // 但会在输出里明确提示，避免"CI 全绿"被误解为"探针可用"。
  { name: "probe", args: [resolve(here, "native", "probe.py"), "--selftest"], python: true },
  // RNG 自检不需要 unicorn，纯算法验证。它锁死了 TH185 最关键的机制。
  { name: "rng", args: [resolve(here, "native", "rng.py"), "--selftest"], python: true },
  // 波次表结构 + 抽取算法。前者需要 exe，后者纯算法。
  { name: "waves", args: [resolve(here, "native", "waves.py"), "--selftest"], python: true },
  { name: "waves-algo", args: [resolve(here, "native", "waves.py"), "--selftest-algo"], python: true },
  { name: "waves-force", args: [resolve(here, "native", "waves.py"), "--selftest-force"], python: true },
  { name: "cards", args: [resolve(here, "native", "cards.py"), "--selftest"], python: true },
  { name: "card-classes", args: [resolve(here, "native", "card_classes.py"), "--selftest"], python: true },
  { name: "lzss", args: [resolve(here, "native", "lzss.py"), "--selftest"], python: true },
  { name: "difficulty", args: [resolve(here, "native", "difficulty.py"), "--selftest"], python: true },
  { name: "pbg4", args: [resolve(here, "native", "pbg4.py"), "--selftest"], python: true },
  { name: "save", args: [resolve(here, "native", "save.py"), "--selftest"], python: true },
  // anm decodes all 55 .anm files (largest is 8.9 MB), so it is the slowest check.
  { name: "anm", args: [resolve(here, "native", "anm.py"), "--selftest"], python: true, timeoutMs: 600000 },
  { name: "anm-texture", args: [resolve(here, "native", "anm_texture_check.py")], python: true, timeoutMs: 600000 },
  { name: "std", args: [resolve(here, "native", "std.py"), "--selftest"], python: true },
  { name: "ecl", args: [resolve(here, "native", "ecl.py"), "--selftest"], python: true },
  { name: "ecl-builtins", args: [resolve(here, "native", "ecl_builtins.py"), "--selftest"], python: true },
  { name: "ecl-coverage", args: [resolve(here, "native", "ecl_coverage.py"), "--selftest"], python: true },
  { name: "ecl-interp", args: [resolve(here, "native", "ecl_interp_check.py")], python: true },
  { name: "ecl-stack", args: [resolve(here, "native", "ecl_stack_infer.py"), "--selftest"], python: true },
  { name: "rng-cpp", args: [resolve(here, "native", "rng_cpp_check.py"), "--selftest"], python: true },
  { name: "archive-cpp", args: [resolve(here, "native", "archive_cpp_check.py"), "--selftest"], python: true },
  { name: "ecl-cpp", args: [resolve(here, "native", "ecl_cpp_check.py"), "--selftest"], python: true },
  { name: "stage-cpp", args: [resolve(here, "native", "stage_cpp_check.py"), "--selftest"], python: true },
  { name: "waves-cpp", args: [resolve(here, "native", "waves_cpp_check.py"), "--selftest"], python: true },
  { name: "enemymanager-cpp", args: [resolve(here, "native", "enemymanager_cpp_check.py"), "--selftest"], python: true },
  { name: "cards-cpp", args: [resolve(here, "native", "cards_cpp_check.py"), "--selftest"], python: true },
  { name: "difficulty-cpp", args: [resolve(here, "native", "difficulty_cpp_check.py"), "--selftest"], python: true },
  { name: "cpp-build", args: [resolve(here, "build-cpp.mjs")], python: false },
  { name: "cpp-runtime", args: [resolve(here, "native", "cpp_runtime_check.py"), "--selftest"], python: true },
  { name: "cpp-archive", args: [resolve(here, "native", "cpp_archive_runtime.py"), "--selftest"], python: true },
  { name: "cpp-session", args: [resolve(here, "native", "cpp_session_runtime.py"), "--selftest"], python: true },
  // The application module is what the web shell loads, so its render is the
  // end-to-end check that everything below it is wired correctly.
  { name: "app-frame", args: [resolve(here, "native", "app_frame_check.py")], python: true, timeoutMs: 900000 },
  // No browser automation here, so the shell is checked by CONTRACT: exports,
  // element ids, asset paths. Those are the failures that actually happen.
  { name: "shell-contract", args: [resolve(here, "native", "shell_contract_check.py")], python: true },
);

let failures = 0;
const width = Math.max(...checks.map((c) => c.name.length));

for (const check of checks) {
  const command = check.python ? "python" : process.execPath;
  const result = spawnSync(command, check.args, {
    cwd: repoRoot,
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
  });
  const ok = result.status === 0;
  const label = ok ? "PASS" : "FAIL";
  console.log(`[${label}] ${check.name.padEnd(width)}  (${check.args.slice(1).join(" ") || "default"})`);

  // progress --stdout 正常时会输出整篇 markdown，太长，只保留摘要
  let output = ((result.stdout || "") + (result.stderr || "")).trim();
  if (check.name === "cpp-build") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.includes("translation unit"));
      output = summary.join("\n");
    }
  }
  if (check.name === "cpp-session") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "cpp-archive") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "cpp-runtime") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "difficulty-cpp") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "cards-cpp") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "waves-cpp") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "stage-cpp") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "ecl-cpp") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "archive-cpp") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "rng-cpp") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "ecl-builtins") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "ecl") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "std") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "pbg4") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed") || line.includes("magic ="));
      output = summary.join("\n");
    }
  }
  if (check.name === "difficulty") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("selftest passed"));
      output = summary.join("\n");
    }
  }
  if (check.name === "lzss") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("自检通过"));
      output = summary.join("\n");
    }
  }
  if (check.name === "cards") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("自检通过") || line.includes("解析"));
      output = summary.join("\n");
    }
  }
  if (check.name === "waves" || check.name === "waves-algo" || check.name === "waves-force") {
    if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("自检通过") || line.startsWith("合计"));
      output = summary.join("\n");
    }
  }
  if (check.name === "rng") {
    if (!ok) {
      // 保留完整输出：RNG 失败意味着算法被动过
    } else {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("自检通过") || line.includes("seed = "));
      output = summary.join("\n");
    }
  }
  if (check.name === "probe") {
    if (!ok && /No module named/.test(output)) {
      console.log("         (跳过：缺少 unicorn/capstone，pip install unicorn capstone)");
      output = "";
    } else if (ok) {
      const summary = output.split("\n").filter((line) =>
        line.startsWith("[PASS]") || line.startsWith("自检") || line.includes("导入函数数"));
      output = summary.join("\n");
    }
  }
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
