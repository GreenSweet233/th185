#!/usr/bin/env python3
"""共享的 PE 读取与 x86 反汇编工具。

供 G2 (check_witness.py) / G3 (check_structure.py) / G6 (check_evidence.py) 复用。

## 原版二进制从哪里来

原版 th185.exe **不入库**（旧规约第 1 条铁律，版权 + 体积）。定位优先级
与 th185_web/scripts/verify-target.mjs 保持一致，单一事实源：

  1. --exe 命令行参数
  2. TH185_EXE 环境变量
  3. /orig/th185.exe（若使用者自己放进去）
  4. th185_web/local.target.json 的 gameDir + th185.exe

拿不到就返回 None，调用方必须把这种情况当作 FAIL（不能静默跳过）。

## 为什么不用 libclang / 其他重型依赖

本环境只有 capstone 5.0.7 + pefile 2024.8.26 + unicorn 2.1.4，没有 libclang。
反汇编走 capstone，PE 结构走 pefile，够 G2/G3 用。
"""

import json
import os

try:
    import pefile
except ImportError:  # pragma: no cover
    pefile = None

try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
except ImportError:  # pragma: no cover
    Cs = None


IMAGE_BASE_EXPECTED = 0x00400000


def find_exe(root, cli_path=None):
    """按上面文档的优先级定位原版 exe。找不到返回 None（调用方判 FAIL）。"""
    if cli_path and os.path.isfile(cli_path):
        return os.path.abspath(cli_path)
    env = os.environ.get("TH185_EXE")
    if env and os.path.isfile(env):
        return os.path.abspath(env)

    orig = os.path.join(root, "orig", "th185.exe")
    if os.path.isfile(orig):
        return orig

    local = os.path.join(root, "th185_web", "local.target.json")
    if os.path.isfile(local):
        try:
            with open(local, encoding="utf-8") as fh:
                game_dir = json.load(fh).get("gameDir")
            if game_dir:
                cand = os.path.join(game_dir, "th185.exe")
                if os.path.isfile(cand):
                    return os.path.abspath(cand)
        except (OSError, json.JSONDecodeError):
            pass
    return None


def load_pe(exe_path):
    """返回 (pe, sections_by_name)。sections_by_name: name -> dict。"""
    if pefile is None:
        raise RuntimeError("pefile 未安装")
    pe = pefile.PE(exe_path, fast_load=True)
    sections = {}
    for s in pe.sections:
        name = s.Name.decode("ascii", "replace").rstrip("\0")
        sections[name] = {
            "name": name,
            "va": s.VirtualAddress,
            "vsize": s.Misc_VirtualSize,
            "raw_ptr": s.PointerToRawData,
            "raw_size": s.SizeOfRawData,
            "data": s.get_data(),
        }
    return pe, sections


def make_disassembler():
    """返回启用了 skipdata 的反汇编器。

    ## 为什么必须 skipdata

    capstone 默认遇到无法解码的字节就**停止**。实测 th185.exe 的 .text：
      默认行为       -> 只反汇编 37854 条指令，覆盖 124029 / 711114 字节 (17.4%)
                        在 0x0041F47D 停下，后面 587085 字节全被丢掉
                        直接 call 目标只有 194 个
      skipdata=True  -> 208003 条指令，覆盖 711114 / 711114 (100.0%)
                        直接 call 目标 1678 个
                        skipdata 伪指令仅 487 条 (0.2%)

    差 8.6 倍的 call 目标。用默认行为建「二进制事实库」等于只看前 17%，
    后面的见证全部会误判为「不存在」。这是 G2 能成立的前提。
    """
    if Cs is None:
        raise RuntimeError("capstone 未安装")
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    md.detail = True
    md.skipdata = True
    return md


def disasm_range(md, code_bytes, base_va, max_instructions=None):
    """反汇编一段代码。返回 [(va, size, mnemonic, op_str, bytes)]。"""
    out = []
    for ins in md.disasm(code_bytes, base_va):
        # skipdata 产出的 ".byte" 是「这里解码失败」的占位，不是指令。
        # 仍然记录它（反向覆盖要算字节），但标记出来，调用方可以过滤。
        if ins.mnemonic == ".byte":
            out.append({
                "va": ins.address, "size": ins.size,
                "mnemonic": ".byte", "op_str": ins.op_str,
                "bytes": ins.bytes.hex(), "is_data": True,
            })
            if max_instructions and len(out) >= max_instructions:
                break
            continue
        out.append({
            "va": ins.address,
            "size": ins.size,
            "mnemonic": ins.mnemonic,
            "op_str": ins.op_str,
            "bytes": ins.bytes.hex(),
            "is_data": False,
        })
        if max_instructions and len(out) >= max_instructions:
            break
    return out


def rva_to_va(pe, rva):
    return pe.OPTIONAL_HEADER.ImageBase + rva


def va_to_rva(pe, va):
    return va - pe.OPTIONAL_HEADER.ImageBase


def find_section_for_va(sections, va, image_base):
    rva = va - image_base
    for name, s in sections.items():
        if s["va"] <= rva < s["va"] + max(s["vsize"], s["raw_size"]):
            return name, rva - s["va"]
    return None, None


def scan_text(sections, image_base, md, max_instructions=None):
    """反汇编整个 .text。

    用 vsize 而不是 raw_size 作为上限：raw_size 是文件对齐后的（0xADA00），
    vsize 是真实代码长度（0xAD9CA），多出来的部分是填充。
    启用 skipdata 后可以从头扫到尾不中断。
    """
    text = sections.get(".text")
    if not text:
        return []
    size = min(text["vsize"], len(text["data"]))
    code = text["data"][:size]
    return disasm_range(md, code, image_base + text["va"], max_instructions)


def call_targets(instructions):
    """提取所有直接 call 的目标 VA。"""
    targets = set()
    for ins in instructions:
        if ins["mnemonic"] == "call" and ins["op_str"].startswith("0x"):
            try:
                targets.add(int(ins["op_str"], 16))
            except ValueError:
                pass
    return targets
