#!/usr/bin/env python3
r"""P1 字节级匹配验证器：编译源码，与 /orig 的 .text 逐字节比对。

## 工作方式

  1. 从 src/ 里找带 //@witness func=0xNNN 的函数
  2. 用 MSVC v142 + 指定 flags 编译成 .obj
  3. 解析 COFF 取 .text 段
  4. 与原版对应地址的字节比对

## 编译器来源（不需要管理员权限）

  D:\MSVC142\14.29.30133  从 ProgramData 缓存里的 .vsix 解压而来
  见 evidence/P1-first-byte-match.json

## 退出码
  0 = 全部字节相等
  1 = 有差异
  2 = 环境错误
"""

import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe_common
import check_witness as cw

MSVC_ROOT = r"D:\MSVC142\14.29.30133"
CL = os.path.join(MSVC_ROOT, "bin", "Hostx86", "x86", "cl.exe")
INCLUDE = os.path.join(MSVC_ROOT, "include")
LIB = os.path.join(MSVC_ROOT, "lib", "x86")
BIN = os.path.join(MSVC_ROOT, "bin", "Hostx86", "x86")


def parse_coff_text(path):
    """从 COFF 目标文件里取代码段字节。"""
    raw = open(path, "rb").read()
    machine, nsec, tds, symptr, nsym, optsz, chars = struct.unpack_from("<HHIIIHH", raw, 0)
    strtab_off = symptr + nsym * 18
    off = 20 + optsz
    best = None
    for i in range(nsec):
        s = raw[off:off+40]
        nb = s[0:8]
        if nb.startswith(b"/"):
            try:
                noff = int(nb[1:].rstrip(b"\0"))
                end = raw.index(b"\0", strtab_off + noff)
                name = raw[strtab_off + noff:end].decode("ascii", "replace")
            except Exception:
                name = "?"
        else:
            name = nb.rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr, relptr, lnptr, nrel, nln, flags = struct.unpack_from(
            "<IIIIIIHHI", s, 8)
        if name.startswith(".text") and (best is None or rawsize > best[1]):
            best = (name, rawsize, raw[rawptr:rawptr + rawsize])
        off += 40
    return best


def compile_cpp(source_text, flags, workdir):
    """编译一段 C++，返回 .obj 路径。"""
    cpp = os.path.join(workdir, "t.cpp")
    obj = os.path.join(workdir, "t.obj")
    with open(cpp, "w", encoding="utf-8") as fh:
        fh.write(source_text)
    env = dict(os.environ)
    env["INCLUDE"] = INCLUDE
    env["LIB"] = LIB
    env["PATH"] = BIN + os.pathsep + env.get("PATH", "")
    cmd = [CL, "/nologo", "/c"] + flags + ["/Fo" + obj, cpp]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                           errors="replace", timeout=120, env=env, cwd=workdir)
    except (subprocess.TimeoutExpired, OSError) as exc:
        return None, "cl.exe failed: %s" % exc
    if not os.path.isfile(obj):
        return None, (p.stdout or "") + (p.stderr or "")
    return obj, ""


def original_bytes(sections, image_base, va, size):
    for name, s in sections.items():
        lo = image_base + s["va"]
        if lo <= va < lo + len(s["data"]):
            off = va - lo
            return s["data"][off:off + size]
    return None


def main():
    ap = argparse.ArgumentParser(description="P1 byte-level match")
    ap.add_argument("--root", default=os.path.dirname(os.path.dirname(
        os.path.abspath(__file__))))
    ap.add_argument("--exe", default=None)
    ap.add_argument("--flags", default="/MT /O2 /GS-")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--write-dashboard", action="store_true")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    if not os.path.isfile(CL):
        print("[FAIL] 找不到 cl.exe: %s" % CL)
        print("       见 evidence/P1-first-byte-match.json 的解包步骤")
        return 2

    exe = pe_common.find_exe(root, args.exe)
    if not exe:
        print("[FAIL] 找不到原版 th185.exe")
        return 1

    pe, sections = pe_common.load_pe(exe)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    md = pe_common.make_disassembler()
    insns = pe_common.scan_text(sections, image_base, md)

    print("=== P1 byte-level match ===")
    print("  compiler: %s" % CL)
    print("  flags   : %s" % args.flags)
    print()

    flags = args.flags.split()
    results = []

    # 每个条目：(标签, 地址, 源文件, 函数名, 额外前缀)
    # 额外前缀用于把 __fastcall 模拟的声明也带进去
    targets = [
        ("0x00402A50", 0x00402A50, "Timer.cpp", "tick_timer", True),
        ("0x00404AA0", 0x00404AA0, "Trivial.cpp", "return_this", True),
        ("0x00404C20", 0x00404C20, "Trivial.cpp", "do_nothing", False),
        ("0x0041FA20", 0x0041FA20, "Trivial.cpp", "read_first_field", True),
        ("0x00404B60", 0x00404B60, "Trivial.cpp", "read_first_float", True),
        ("0x00404B70", 0x00404B70, "Trivial.cpp", "polar_to_vec2", True),
        ("0x004903B9", 0x004903B9, "Trivial.cpp", "return_zero", False),
        ("0x00405830", 0x00405830, "Trivial.cpp", "copy_vec3", True),
        ("0x00406720", 0x00406720, "Trivial.cpp", "set_first_field", True),
        ("0x00417750", 0x00417750, "Trivial.cpp", "polar_to_vec2_scaled", True),
        ("0x0049DC88", 0x0049DC88, "Trivial.cpp", "encode_value", False),
    ]

    for label, va, src_name, fn_name, need_this in targets:
        lo, hi = cw.function_extent(label, insns)
        if lo is None:
            results.append({"func": label, "status": "ERROR", "error": "no extent"})
            continue
        orig = original_bytes(sections, image_base, lo, hi - lo)

        src_path = os.path.join(root, "src", src_name)
        if not os.path.isfile(src_path):
            results.append({"func": label, "status": "SKIP", "error": "no " + src_name})
            continue
        with open(src_path, encoding="utf-8") as fh:
            src = fh.read()

        # 提取单个函数：从 //@witness func= 之后到下一个 //@witness 或文件尾
        lines = src.split("\n")
        idx = None
        for k, ln in enumerate(lines):
            if "//@witness func=" in ln:
                # 该 witness 块所属的函数名是否匹配
                # 向后找第一个函数定义
                for j in range(k + 1, min(k + 12, len(lines))):
                    if fn_name + "(" in lines[j].replace(" ", "") or \
                       (fn_name + " (") in lines[j]:
                        idx = j
                        break
                if idx is not None:
                    break
        if idx is None:
            results.append({"func": label, "status": "SKIP",
                            "error": "cannot locate %s in %s" % (fn_name, src_name)})
            continue

        # 从函数定义起，做花括号配平
        depth = 0
        started = False
        body_lines = []
        for j in range(idx, len(lines)):
            ln = lines[j]
            body_lines.append(ln)
            depth += ln.count("{") - ln.count("}")
            if "{" in ln:
                started = True
            if started and depth == 0:
                break

        func_text = "\n".join(body_lines)

        # 如果是 __fastcall 模拟，需要把调用约定写进去（源文件里可能没写）
        if need_this and "__fastcall" not in func_text:
            # 在函数名前的返回类型后插入 __fastcall
            import re as _re
            func_text = _re.sub(r"(\n)([A-Za-z_][\w\s\*]*?)\s+" + fn_name + r"\s*\(",
                                r"\1\2 __fastcall " + fn_name + "(", func_text, count=1)

        # 前置需要的常量定义（从 hpp 里抠 constexpr）
        prefix = ""
        hpp_path = os.path.join(root, "include",
                                src_name.replace(".cpp", ".hpp"))
        if os.path.isfile(hpp_path):
            with open(hpp_path, encoding="utf-8") as fh:
                for ln in fh.read().split("\n"):
                    s = ln.strip()
                    if s.startswith("constexpr") or s.startswith("static const"):
                        prefix += ln + "\n"

        body = prefix + "\n" + func_text
        body = "\n".join(ln for ln in body.split("\n")
                          if not ln.strip().startswith("#include")
                          and not ln.strip().startswith("#pragma")
                          and "//@witness" not in ln)

        with tempfile.TemporaryDirectory() as td:
            obj, err = compile_cpp(body, flags, td)
            if not obj:
                results.append({"func": label, "status": "COMPILE_ERROR", "error": err[:500]})
                continue
            parsed = parse_coff_text(obj)
            if not parsed:
                results.append({"func": label, "status": "COFF_ERROR"})
                continue
            _, _, code = parsed

        same = (code == orig)
        entry = {
            "func": label,
            "status": "MATCH" if same else "DIFF",
            "orig_len": len(orig),
            "ours_len": len(code),
        }
        if not same:
            n = 0
            while n < min(len(code), len(orig)) and code[n] == orig[n]:
                n += 1
            entry["common_prefix"] = n
            entry["orig"] = orig.hex()
            entry["ours"] = code.hex()
        results.append(entry)

        print("  %-14s %-8s  orig=%d ours=%d %s" % (
            label, entry["status"], len(orig), len(code),
            "" if same else "(prefix %d)" % entry.get("common_prefix", 0)))

    print()
    n_match = sum(1 for r in results if r["status"] == "MATCH")
    n_total = len(results)
    print("  结果: %d / %d 字节相等" % (n_match, n_total))
    if n_match < n_total:
        for r in results:
            if r["status"] != "MATCH":
                print("  [FAIL] %s: %s" % (r["func"], r["status"]))
                if r.get("error"):
                    print("         %s" % r["error"][:200])
                if r.get("orig"):
                    print("         orig: %s" % r["orig"][:120])
                    print("         ours: %s" % r["ours"][:120])

    status = "PASS" if n_match == n_total and n_total > 0 else "FAIL"
    print()
    print("[%s] P1" % status)

    if args.write_dashboard:
        write_dashboard(root, results, status)

    return 0 if status == "PASS" else 1


def write_dashboard(root, results, status):
    path = os.path.join(root, "dashboard", "metrics.json")
    try:
        with open(path, encoding="utf-8") as fh:
            m = json.load(fh)
    except (OSError, json.JSONDecodeError):
        m = {}
    n_match = sum(1 for r in results if r["status"] == "MATCH")
    m["byte_match"] = {
        "status": status,
        "matched": n_match,
        "verified": len(results),
        "_note": "只覆盖能单独编译验证的函数；其余仍只有静态门槛",
    }
    m["updated_at"] = __import__("datetime").datetime.now().isoformat(timespec="seconds")
    try:
        with open(path, "w", encoding="utf-8") as fh:
            json.dump(m, fh, indent=2, ensure_ascii=False)
    except OSError as exc:
        print("  (warn: %s)" % exc)


if __name__ == "__main__":
    sys.exit(main())