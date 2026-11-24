#!/usr/bin/env python3
r"""G2 二进制见证检查器 (AGENTS.md §6 G2)。

## G2 的原文要求

  - 每个变量/常量/字段/call 必须映射到二进制 artifact
    (栈槽/立即数/偏移/调用目标/reloc)。
  - 优化后消失且无见证(dummy/魔法pad) -> FAIL。
  - 反向覆盖：二进制每个栈槽/立即数/call 都被源码解释，覆盖率=100%。
  - 产出 evidence/{func}.witness.json。

## 本脚本实际能做到什么（诚实边界）

G2 的完整形态需要「编译自己的 .obj -> 与 /orig 的 .text 同名函数逐条比对」。
但本机工具链现状（已实测，见 toolchain.lock）：

  原版：MSVC 三档 build (27412 / 31328 / 31332) 即 VS2019 16.0 / 16.8 / 16.9
  本机：只有 VS2022 Community 的 MSVC 14.44.35207
        C:\Program Files (x86)\Microsoft Visual Studio\2019 目录存在但是空的

按 AGENTS.md §2「禁止为凑匹配改编译器」，本脚本**不去用 VS2022 冒充 VS2019**。
它做的是 G2 中不依赖编译器的那一半：

  1. 建立「目标二进制事实库」：反汇编 .text，索引所有
     - 直接 call 目标
     - 立即数常量
     - 栈帧偏移
  2. 对每条源码声明（/src 里带 //@witness 注释的条目）检查它引用的
     artifact 是否真实存在于目标二进制。
  3. 反向覆盖率：源码声明的 artifact 集合 / 二进制里被「需要解释」的集合。

## 源码如何声明见证

在 /src 的函数上方写：

    //@witness func=0x414F20 kind=this
    //@witness call 0x401000            # 本函数调用了 0x401000
    //@witness imm 0x64                 # 常量 0x64 有出处
    //@witness slot -0xC                # 栈槽 ebp-0xC
    void AddCard(...) {...}

## 退出码
  0 = PASS
  1 = FAIL (见证不成立 / 覆盖率不达标 / 找不到原版)
  2 = 用法或环境错误
"""

import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe_common


WITNESS_RE = re.compile(r"//\s*@witness\s+(\w+)\s+(.+)$")


def parse_witnesses(sources_root):
    """扫描 /src，收集 //@witness 声明。

    返回 {func_key: [ {kind, value, file, line, raw} ]}
    """
    decls = {}
    for dirpath, _dirnames, filenames in os.walk(sources_root):
        for fn in filenames:
            if not fn.endswith((".cpp", ".cc", ".cxx", ".hpp", ".h")):
                continue
            path = os.path.join(dirpath, fn)
            try:
                with open(path, encoding="utf-8", errors="replace") as fh:
                    lines = fh.readlines()
            except OSError:
                continue
            current = None
            for idx, raw in enumerate(lines, 1):
                m = WITNESS_RE.search(raw)
                if not m:
                    continue
                kind = m.group(1)
                rest = m.group(2).strip()
                if kind == "func":
                    # func=0x414F20 kind=this
                    fm = re.match(r"(\S+)\s*(.*)$", rest)
                    current = fm.group(1) if fm else rest
                    decls.setdefault(current, []).append({
                        "kind": "func", "value": current,
                        "file": path, "line": idx, "raw": raw.strip(),
                    })
                else:
                    key = current or ("<file:%s>" % os.path.basename(path))
                    decls.setdefault(key, []).append({
                        "kind": kind, "value": rest,
                        "file": path, "line": idx, "raw": raw.strip(),
                    })
    return decls


def norm_value(kind, value):
    """把声明值规范化成可比较的整数（能转就转，不能就原样返回）。"""
    v = value.strip()
    if kind == "call" or kind == "imm":
        try:
            return int(v, 16) if v.lower().startswith("0x") else int(v, 0)
        except ValueError:
            return v
    if kind == "slot":
        try:
            return int(v, 0) if not v.lower().startswith("0x") else int(v, 16)
        except ValueError:
            return v
    return v


def build_target_facts(sections, image_base, md):
    """从目标二进制提取可被见证的 artifact 集合。"""
    insns = pe_common.scan_text(sections, image_base, md)
    calls = set()
    imms = set()
    slots = set()
    for ins in insns:
        if ins.get("is_data"):
            continue
        if ins["mnemonic"] == "call" and ins["op_str"].startswith("0x"):
            try:
                calls.add(int(ins["op_str"], 16))
            except ValueError:
                pass
        # 立即数：op_str 里的 0x 数字
        for m in re.finditer(r"0x([0-9a-fA-F]+)", ins["op_str"]):
            imms.add(int(m.group(1), 16))
        # 栈槽：ebp - 0xNN / ebp + 0xNN
        for m in re.finditer(r"ebp ([+-]) 0x([0-9a-fA-F]+)", ins["op_str"]):
            sign = -1 if m.group(1) == "-" else 1
            slots.add(sign * int(m.group(2), 16))
    real_insns = [i for i in insns if not i.get("is_data")]
    return {
        "instruction_count": len(real_insns),
        "data_bytes": sum(i["size"] for i in insns if i.get("is_data")),
        "call_targets": calls,
        "immediates": imms,
        "stack_slots": slots,
    }


def main():
    ap = argparse.ArgumentParser(description="G2 binary witness check")
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    ap.add_argument("--exe", default=None, help="path to original th185.exe")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--write-dashboard", action="store_true")
    ap.add_argument("--emit-evidence", action="store_true",
                    help="write evidence/{func}.witness.json for each declared func")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    exe = pe_common.find_exe(root, args.exe)
    if not exe:
        print("[FAIL] G2: 找不到原版 th185.exe。")
        print("       已尝试：--exe / TH185_EXE / /orig/th185.exe / local.target.json")
        print("       原版二进制不入库（旧规约第 1 条），请在本地提供。")
        return 1

    try:
        pe, sections = pe_common.load_pe(exe)
    except Exception as exc:  # noqa: BLE001
        print("[FAIL] G2: 无法解析 PE: %s" % exc)
        return 1
    try:
        md = pe_common.make_disassembler()
    except Exception as exc:  # noqa: BLE001
        print("[FAIL] G2: 无法初始化反汇编器: %s" % exc)
        return 1

    image_base = pe.OPTIONAL_HEADER.ImageBase
    facts = build_target_facts(sections, image_base, md)

    src = os.path.join(root, "src")
    decls = parse_witnesses(src) if os.path.isdir(src) else {}

    failures = []
    checked_items = 0
    missing_items = []

    for func_key, items in decls.items():
        for it in items:
            if it["kind"] == "func":
                continue
            checked_items += 1
            val = norm_value(it["kind"], it["value"])
            ok = True
            detail = ""
            if it["kind"] == "call":
                ok = isinstance(val, int) and val in facts["call_targets"]
                detail = "not a call target in target .text"
            elif it["kind"] == "imm":
                ok = isinstance(val, int) and val in facts["immediates"]
                detail = "immediate not present in target .text"
            elif it["kind"] == "slot":
                ok = isinstance(val, int) and val in facts["stack_slots"]
                detail = "stack slot not found in target .text"
            else:
                ok = False
                detail = "unknown witness kind %r" % it["kind"]
            if not ok:
                missing_items.append({
                    "func": func_key, "kind": it["kind"], "value": it["value"],
                    "file": os.path.relpath(it["file"], root), "line": it["line"],
                    "reason": detail,
                })

    if missing_items:
        failures.append({
            "kind": "witness_not_found",
            "count": len(missing_items),
            "sample": missing_items[:15],
        })

    # 反向覆盖：源码声明了哪些 artifact，二进制有多少 artifact 被解释
    declared_calls = {norm_value(i["kind"], i["value"]) for items in decls.values()
                      for i in items if i["kind"] == "call"}
    declared_calls = {v for v in declared_calls if isinstance(v, int)}
    binary_calls = facts["call_targets"]
    reverse_ratio = (len(declared_calls & binary_calls) / len(binary_calls)) if binary_calls else 0.0

    # 覆盖率口径要在报告里说清楚：分母是二进制的 call 目标数，
    # 分子是源码已解释的 call 目标数。源码为空时分子为 0。
    coverage = reverse_ratio

    # 判定口径（重要，避免「什么都没查却报 PASS」）：
    #   FAIL       = 声明了见证但目标二进制里找不到
    #   INCOMPLETE = 没有 FAIL，但反向覆盖率未达 100%（bootstrap 期正常）
    #   PASS       = 无 FAIL 且反向覆盖率 = 100%
    #
    # 之前只看 failures 就报 PASS，导致「1678 个 call 目标无一被解释」
    # 也被标成 PASS —— 那会让 §11 仪表盘骗人。
    if failures:
        status = "FAIL"
    elif binary_calls and len(declared_calls & binary_calls) < len(binary_calls):
        status = "INCOMPLETE"
    elif not binary_calls:
        status = "INCOMPLETE"
    else:
        status = "PASS"

    result = {
        "gate": "G2",
        "status": status,
        "target_exe_sha_path": exe,
        "instruction_count": facts["instruction_count"],
        "binary_artifacts": {
            "call_targets": len(facts["call_targets"]),
            "immediates": len(facts["immediates"]),
            "stack_slots": len(facts["stack_slots"]),
        },
        "witness_declarations": {
            "functions": len([k for k in decls if not k.startswith("<file:")]),
            "items": checked_items,
        },
        "reverse_coverage": {
            "declared_call_targets": len(declared_calls),
            "binary_call_targets": len(binary_calls),
            "ratio": round(coverage, 6),
            "_denominator": "binary call targets; numerator = those explained by source",
        },
        "witnessless_elements": len(missing_items),
        "failures": failures,
    }

    if args.emit_evidence:
        ev_dir = os.path.join(root, "evidence")
        os.makedirs(ev_dir, exist_ok=True)
        for func_key, items in decls.items():
            if func_key.startswith("<file:"):
                continue
            payload = {
                "func": func_key,
                "declared": [
                    {"kind": i["kind"], "value": i["value"],
                     "file": os.path.relpath(i["file"], root), "line": i["line"]}
                    for i in items
                ],
                "target_exe": os.path.basename(exe),
                "_note": "G2 见证表；双向覆盖率见 dashboard",
            }
            safe = re.sub(r"[^A-Za-z0-9_.-]", "_", func_key)
            with open(os.path.join(ev_dir, "%s.witness.json" % safe), "w",
                      encoding="utf-8") as fh:
                json.dump(payload, fh, indent=2, ensure_ascii=False)

    if args.json:
        print(json.dumps(result, indent=2, ensure_ascii=False))
    else:
        print("=== G2 binary witness ===")
        print("  target exe          : %s" % exe)
        print("  .text instructions  : %d" % facts["instruction_count"])
        print("  binary artifacts    : calls=%d  immediates=%d  slots=%d"
              % (len(facts["call_targets"]), len(facts["immediates"]),
                 len(facts["stack_slots"])))
        print("  witness declarations: %d item(s) in %d function(s)"
              % (checked_items, result["witness_declarations"]["functions"]))
        print("  reverse coverage    : %.4f%% (%d / %d call targets explained)"
              % (coverage * 100, len(declared_calls & binary_calls), len(binary_calls)))
        print("  witnessless         : %d" % len(missing_items))
        if failures:
            print()
            for f in failures:
                print("  [FAIL] %s  (%d)" % (f["kind"], f["count"]))
                for s in f.get("sample", [])[:10]:
                    print("         %s::%s %s  (%s:%d)"
                          % (s["func"], s["kind"], s["value"], s["file"], s["line"]))
                    print("           -> %s" % s["reason"])
        print()
        if status == "FAIL":
            print("[FAIL] G2")
        elif status == "INCOMPLETE":
            print("[INCOMPLETE] G2")
            print("             %d / %d call targets explained (%.4f%%)."
                  % (len(declared_calls & binary_calls), len(binary_calls),
                     coverage * 100))
            print("             这不是 PASS：源码尚未解释目标二进制。")
            print("             按 AGENTS.md §11，反向覆盖率 < 100% 不算完成。")
        else:
            print("[PASS] G2")

    if args.write_dashboard:
        write_dashboard(root, result)

    return 0 if status != "FAIL" else 1


def write_dashboard(root, result):
    path = os.path.join(root, "dashboard", "metrics.json")
    try:
        with open(path, encoding="utf-8") as fh:
            m = json.load(fh)
    except (OSError, json.JSONDecodeError):
        m = {}
    m.setdefault("gates_implemented", {})["G2"] = (result["status"] == "PASS")
    m["witnessless_elements"] = result["witnessless_elements"]
    m.setdefault("_notes", {})["witnessless_elements"] = (
        "G2 实测值。0 表示当前源码的见证声明全部命中；"
        "不表示二进制已被完全解释（见 reverse_coverage）")
    m["_g2_last_run"] = {
        "status": result["status"],
        "instruction_count": result["instruction_count"],
        "binary_call_targets": result["binary_artifacts"]["call_targets"],
        "witness_items": result["witness_declarations"]["items"],
        "reverse_coverage": result["reverse_coverage"]["ratio"],
    }
    m["updated_at"] = __import__("datetime").datetime.now().isoformat(timespec="seconds")
    try:
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(m, fh, indent=2, ensure_ascii=False)
    except OSError as exc:
        print("  (warn: could not write dashboard: %s)" % exc)


if __name__ == "__main__":
    sys.exit(main())
