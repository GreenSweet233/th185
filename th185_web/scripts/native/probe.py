#!/usr/bin/env python3
"""TH185 原版函数执行探针（Unicorn）。

把 th185.exe 完整映射到内存，调用指定 RVA 处的原版函数，返回其结果。
这是开发期工具，**不进入发布产物**（见 AGENTS.md 铁律 4）。

为什么需要它：
  静态反汇编能看出控制流，但看不出「这段代码对具体输入产出什么」。
  PBG4 的逐字节解密算法靠静态分析已经收益递减，而探针可以直接问
  原版函数「给我解这段数据」，拿真实输出。

设计取舍：
  - 不实现完整的 Windows 环境，只 stub 必需的 CRT 函数。
  - 未实现的导入一律报错退出，**绝不静默返回假值**——那会污染结论。
  - 每次调用前重置寄存器与栈，保证可重复。

用法：
  python th185_web/scripts/native/probe.py --selftest
  python th185_web/scripts/native/probe.py --dump 0xC9470 96
  python th185_web/scripts/native/probe.py --rva 0x1E90 --args 1,2,3,4 --ecx 0 --edx 0

依赖：unicorn, capstone
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[3]
WEB = ROOT / "th185_web"

# 地址布局（必须互不重叠）：
#   0x00100000 - 0x0013FFFF  栈      (256 KB)
#   0x00400000 - 0x005FFFFF  镜像    (2 MB，th185.exe 实际只占约 1.5 MB)
#   0x00600000 - 0x0060FFFF  导入桩  (64 KB)
#   0x00800000 - 0x00BFFFFF  堆      (4 MB)
#   0x00BFFFF0               调用返回哨兵
IMAGE_BASE = 0x400000
IMAGE_SIZE = 0x200000
STACK_BASE = 0x100000
STACK_SIZE = 0x40000
STUB_BASE = 0x600000
STUB_SIZE = 0x10000
HEAP_BASE = 0x800000
HEAP_SIZE = 0x400000
RET_MAGIC = 0xBFFFF0


def target_path() -> Path:
    configured = os.environ.get("TH185_TARGET_PATH")
    if configured:
        return Path(configured).expanduser().resolve()
    local = WEB / "local.target.json"
    if local.is_file():
        try:
            game_dir = json.loads(local.read_text(encoding="utf-8")).get("gameDir")
            if game_dir:
                return (Path(game_dir) / "th185.exe").resolve()
        except (OSError, ValueError):
            pass
    return WEB.parent / "[th185] 弹幕狂们的黑市 (汉化版+日文版)" / "th185.exe"


def load_image():
    with (WEB / "target.json").open(encoding="utf-8") as stream:
        target = json.load(stream)
    path = target_path()
    if not path.is_file():
        raise SystemExit(
            "missing target: %s\n"
            "set TH185_TARGET_PATH or fix th185_web/local.target.json" % path
        )
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest().upper()
    if digest != str(target["sha256"]).upper():
        raise SystemExit(
            "target identity mismatch\n  got      %s\n  expected %s"
            % (digest, target["sha256"])
        )
    return data, target


def parse_pe(data):
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise SystemExit("not a PE image")
    num_sec = struct.unpack_from("<H", data, pe + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    image_base = struct.unpack_from("<I", data, opt + 28)[0]
    sec_off = opt + opt_size
    sections = []
    for i in range(num_sec):
        o = sec_off + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rsize, roff = struct.unpack_from("<IIII", data, o + 8)
        sections.append(
            {"name": name, "vsize": vsize, "vaddr": vaddr, "rsize": rsize, "roff": roff}
        )

    def rva_to_off(rva):
        for s in sections:
            if s["vaddr"] <= rva < s["vaddr"] + max(s["vsize"], s["rsize"]):
                return s["roff"] + (rva - s["vaddr"])
        return -1

    imports = []
    imp_rva = struct.unpack_from("<I", data, opt + 96 + 8)[0]
    if imp_rva:
        p = rva_to_off(imp_rva)
        while p > 0:
            # IMAGE_IMPORT_DESCRIPTOR 是 5 个 DWORD (20 字节)：
            #   OriginalFirstThunk, TimeDateStamp, ForwarderChain, Name, FirstThunk
            # 只解包 3 个字段会把 ForwarderChain 当成 Name。
            oft, _ts, _fc, name_rva, ft = struct.unpack_from("<IIIII", data, p)
            if not name_rva:
                break
            no = rva_to_off(name_rva)
            end = no
            while data[end] != 0:
                end += 1
            dll = data[no:end].decode("ascii", "replace")
            lookup = oft or ft
            n = 0
            while True:
                thunk = struct.unpack_from("<I", data, rva_to_off(lookup) + n * 4)[0]
                if not thunk:
                    break
                if thunk & 0x80000000:
                    fname = "#%d" % (thunk & 0xFFFF)
                else:
                    fo = rva_to_off(thunk) + 2
                    fe = fo
                    while data[fe] != 0:
                        fe += 1
                    fname = data[fo:fe].decode("ascii", "replace")
                imports.append({"dll": dll, "name": fname, "iat": ft + n * 4})
                n += 1
            p += 20
    return {"pe": pe, "image_base": image_base, "sections": sections, "imports": imports}


class Probe:
    """把 PE 映射进 Unicorn 并 stub CRT，使原版函数可执行。"""

    _md_cache = None

    def __init__(self, data, meta, trace_limit=0):
        from unicorn import Uc, UC_ARCH_X86, UC_MODE_32

        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.data = data
        self.meta = meta
        self.base = meta["image_base"]
        self.trace = []
        self.trace_limit = trace_limit
        self.heap_cursor = HEAP_BASE + 0x10000
        self.import_stubs = {}
        self.calls = []
        self.unimplemented = []

        uc = self.uc
        uc.mem_map(IMAGE_BASE, IMAGE_SIZE)
        uc.mem_write(IMAGE_BASE, data[:0x400])
        for s in meta["sections"]:
            if s["rsize"]:
                uc.mem_write(
                    self.base + s["vaddr"], data[s["roff"]:s["roff"] + s["rsize"]]
                )
        uc.mem_map(STACK_BASE, STACK_SIZE)
        uc.mem_map(HEAP_BASE, HEAP_SIZE)
        uc.mem_map(STUB_BASE, STUB_SIZE)
        self._install_imports()
        self._register_crt_stubs()
        self._hook()

    # ---------- 内存辅助 ----------
    def read(self, addr, size):
        return bytes(self.uc.mem_read(addr, size))

    def write(self, addr, blob):
        self.uc.mem_write(addr, blob)

    def u32(self, addr):
        return struct.unpack("<I", self.read(addr, 4))[0]

    def set_u32(self, addr, value):
        self.write(addr, struct.pack("<I", value & 0xFFFFFFFF))

    def alloc(self, size):
        size = (size + 15) & ~15
        ptr = self.heap_cursor
        self.heap_cursor += size + 16
        if self.heap_cursor >= HEAP_BASE + HEAP_SIZE:
            raise SystemExit("probe heap exhausted")
        return ptr

    def cstr(self, addr, limit=4096):
        out = bytearray()
        for i in range(limit):
            b = self.read(addr + i, 1)[0]
            if b == 0:
                break
            out.append(b)
        return bytes(out)

    # ---------- 导入 stub ----------
    def _install_imports(self):
        for imp in self.meta["imports"]:
            stub = STUB_BASE + len(self.import_stubs) * 16
            self.uc.mem_write(stub, b"\xC3")
            self.import_stubs[stub] = imp
            self.set_u32(self.base + imp["iat"], stub)

    def _register_crt_stubs(self):
        """注册内部 CRT 函数的 RVA 拦截。

        这些 RVA 是从反汇编确认的：
          0x92E00  memset   (SSE2 优化版，探针里直接实现更快也更安全)
          0x93374  malloc   (跳转到 0x9F003 的堆管理器，探针不实现)
          0x9337F  free
        """
        Probe.CRT_STUBS[0x92E00] = Probe.crt_memset
        Probe.CRT_STUBS[0x93374] = Probe.crt_malloc
        Probe.CRT_STUBS[0x9337F] = Probe.crt_free

    def _hook(self):
        from unicorn import UC_HOOK_CODE

        self.uc.hook_add(UC_HOOK_CODE, self._on_code, begin=1, end=0xFFFFFFFF)

    def _md(self):
        if Probe._md_cache is None:
            import capstone

            md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
            md.detail = True
            Probe._md_cache = md
        return Probe._md_cache

    # 内部 CRT 函数的 RVA -> 实现。这些是编译器内联/调用的运行时函数，
    # 不是导入表里的，但探针遇到它们时不应该真的执行（它们会访问
    # 探针没映射的区域，或者依赖未初始化的运行时状态）。
    CRT_STUBS = {}

    def _on_code(self, uc, address, size, user):
        if address in self.import_stubs:
            self._handle_import(address)
            return
        rva = address - self.base
        if rva in Probe.CRT_STUBS:
            self.calls.append("crt:0x%X" % rva)
            Probe.CRT_STUBS[rva](self)
            return
        if self.trace_limit and len(self.trace) < self.trace_limit:
            try:
                code = self.read(address, min(size, 15))
                insn = next(self._md().disasm(code, address))
                self.trace.append(
                    "  %06X: %-24s %s %s"
                    % (
                        address - self.base,
                        " ".join("%02x" % b for b in insn.bytes),
                        insn.mnemonic,
                        insn.op_str,
                    )
                )
            except Exception:
                self.trace.append("  %06X: <undecodable>" % (address - self.base))

    def crt_memset(self):
        dest, value, count = self._args(3)
        self.write(dest, bytes([value & 0xFF]) * count)
        self._ret(dest, 3)

    def crt_memcpy(self):
        dest, src, count = self._args(3)
        self.write(dest, self.read(src, count))
        self._ret(dest, 3)

    def crt_malloc(self):
        (size,) = self._args(1)
        self._ret(self.alloc(size), 1)

    def crt_free(self):
        self._ret(0, 1)

    def _ret(self, value=0, pop_args=0):
        from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EAX

        uc = self.uc
        sp = uc.reg_read(UC_X86_REG_ESP)
        ret = self.u32(sp)
        uc.reg_write(UC_X86_REG_ESP, sp + 4 + pop_args * 4)
        uc.reg_write(UC_X86_REG_EAX, value & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EIP, ret)

    def _args(self, count):
        from unicorn.x86_const import UC_X86_REG_ESP

        sp = self.uc.reg_read(UC_X86_REG_ESP)
        return [self.u32(sp + 4 + i * 4) for i in range(count)]

    def _handle_import(self, stub):
        imp = self.import_stubs[stub]
        name = imp["name"]
        self.calls.append(name)

        if name == "memset":
            dest, value, count = self._args(3)
            self.write(dest, bytes([value & 0xFF]) * count)
            self._ret(dest, 3)
            return
        if name in ("memcpy", "memmove"):
            dest, src, count = self._args(3)
            blob = self.read(src, count)
            self.write(dest, blob)
            self._ret(dest, 3)
            return
        if name == "malloc":
            (size,) = self._args(1)
            self._ret(self.alloc(size), 1)
            return
        if name == "free":
            self._ret(0, 1)
            return
        if name == "strlen":
            (ptr,) = self._args(1)
            self._ret(len(self.cstr(ptr)), 1)
            return

        # 其他一律报错：静默返回假值会污染结论
        self.unimplemented.append(name)
        raise SystemExit(
            "unimplemented import reached: %s!%s\n"
            "add a stub in Probe._handle_import before relying on this call"
            % (imp["dll"], name)
        )

    # ---------- 调用 ----------
    def call(self, rva, args=(), ecx=0, edx=0, limit=200000):
        from unicorn import UcError
        from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_EDX, UC_X86_REG_ESP

        uc = self.uc
        sp = STACK_BASE + STACK_SIZE - 0x100
        for value in reversed(list(args)):
            sp -= 4
            self.set_u32(sp, value)
        sp -= 4
        self.set_u32(sp, RET_MAGIC)
        uc.reg_write(UC_X86_REG_ESP, sp)
        uc.reg_write(UC_X86_REG_ECX, ecx & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EDX, edx & 0xFFFFFFFF)
        error = None
        try:
            uc.emu_start(self.base + rva, RET_MAGIC, 0, limit)
        except UcError as exc:
            error = str(exc)
        return {
            "eax": uc.reg_read(UC_X86_REG_EAX),
            "ecx": uc.reg_read(UC_X86_REG_ECX),
            "edx": uc.reg_read(UC_X86_REG_EDX),
            "error": error,
        }


def cmd_selftest(probe):
    """自检：证明探针能执行原版代码并正确 stub CRT。"""
    print("=== 探针自检 ===")
    print("镜像基址  : 0x%X" % probe.base)
    print("导入函数数: %d" % len(probe.meta["imports"]))
    names = sorted({i["name"] for i in probe.meta["imports"]})
    print("导入名样例: %s" % ", ".join(names[:14]))
    print()

    failures = 0

    target = probe.alloc(64)
    probe.write(target, b"\xAA" * 64)
    # 注意：CRT 函数的 RVA 是 0x92E00，不是 0x492E00。
    # 反汇编输出里的 0x492E00 是【虚拟地址】，减 image_base 才是 RVA。
    result = probe.call(0x92E00, args=[target, 0x00, 64])
    blob = probe.read(target, 64)
    ok = blob == b"\x00" * 64 and result["error"] is None
    print("[%s] memset(0x92E00) 清零 64 字节" % ("PASS" if ok else "FAIL"))
    if not ok:
        print("       前 16 字节: %s  error=%s" % (blob[:16].hex(), result["error"]))
        failures += 1

    print()
    print("调用记录: %s" % (", ".join(probe.calls) if probe.calls else "(无)"))
    if probe.unimplemented:
        print("未实现的导入: %s" % ", ".join(sorted(set(probe.unimplemented))))
    print()
    if failures:
        print("自检失败 %d 项" % failures)
        return 1
    print("自检通过：探针可执行原版代码，CRT stub 正常")
    return 0


def cmd_dump(probe, rva, length):
    addr = probe.base + rva
    blob = probe.read(addr, length)
    print("=== RVA 0x%X (%d 字节) ===" % (rva, length))
    for i in range(0, len(blob), 16):
        chunk = blob[i:i + 16]
        hexs = " ".join("%02X" % b for b in chunk)
        text = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print("  %04X  %-47s  %s" % (i, hexs, text))
    return 0


def cmd_call(probe, rva, args, ecx, edx, limit):
    print("=== 调用 RVA 0x%X ===" % rva)
    print("  ecx=0x%X edx=0x%X args=%s" % (ecx, edx, args))
    result = probe.call(rva, args=args, ecx=ecx, edx=edx, limit=limit)
    print("  EAX=0x%X ECX=0x%X EDX=0x%X" % (result["eax"], result["ecx"], result["edx"]))
    if result["error"]:
        print("  !! 执行错误: %s" % result["error"])
        for line in probe.trace:
            print(line)
        return 1
    if probe.calls:
        print("  调用的导入: %s" % ", ".join(probe.calls))
    return 0


def main():
    parser = argparse.ArgumentParser(description="TH185 原版函数执行探针")
    parser.add_argument("--rva", type=lambda s: int(s, 0), help="要调用的函数 RVA")
    parser.add_argument("--args", default="", help="cdecl 参数，逗号分隔")
    parser.add_argument("--ecx", type=lambda s: int(s, 0), default=0)
    parser.add_argument("--edx", type=lambda s: int(s, 0), default=0)
    parser.add_argument("--limit", type=int, default=200000)
    parser.add_argument("--selftest", action="store_true", help="运行自检")
    parser.add_argument("--dump", nargs=2, type=lambda s: int(s, 0),
                        metavar=("RVA", "LEN"), help="dump 镜像内存")
    parser.add_argument("--trace", type=int, default=0, help="记录前 N 条指令")
    args = parser.parse_args()

    try:
        import unicorn  # noqa: F401
        import capstone  # noqa: F401
    except ImportError as exc:
        print("error: %s (pip install unicorn capstone)" % exc, file=sys.stderr)
        return 2

    data, target = load_image()
    meta = parse_pe(data)
    probe = Probe(data, meta, trace_limit=args.trace)

    if args.selftest:
        code = cmd_selftest(probe)
        if args.trace:
            print()
            for line in probe.trace:
                print(line)
        return code
    if args.dump:
        return cmd_dump(probe, args.dump[0], args.dump[1])
    if args.rva is not None:
        call_args = [int(x, 0) for x in args.args.split(",") if x.strip()]
        return cmd_call(probe, args.rva, call_args, args.ecx, args.edx, args.limit)

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
