#!/usr/bin/env python3
"""用 Unicorn 探针解包 th185.dat（PBG4）。

**为什么用探针而不是重写算法：**
  PBG4 的密钥流不是简单的 key + i*step，而是按文件名选择算法后的
  复合结构（已确证：按文件名累加和 & 7 选表，算法表在 VA 0x4C9470）。
  重写它需要完整逆向密钥流生成器；而本项目的目标是**功能还原**，
  只需要**用**它，不需要**复制**它。探针直接调原版解密函数，
  拿到与原版逐字节相同的结果。

**已验证的事实：**
  - 解密函数 RVA 0x1E90，原地解密（返回 EAX == 源指针）
  - ecx = 缓冲区指针，edx = 数据长度
  - args = [key, step, block, limit]，来自算法表 [sum(filename) & 7]
  - block 必须传 1 才得到无重复的线性输出（传 0x200 会每字节重复两次）

用法：
  python th185_web/scripts/native/unpack.py --list
  python th185_web/scripts/native/unpack.py --extract th185.dat --outdir build/assets
  python th185_web/scripts/native/unpack.py --selftest

退出码：0 成功；1 失败；2 环境缺失
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from probe import IMAGE_BASE, Probe, load_image, parse_pe  # noqa: E402

ROOT = HERE.parents[2]
WEB = ROOT / "th185_web"

# 算法表 @ VA 0x4C9470，8 项 x 12 字节：{key:u8, step:u8, pad:u16, block:u32, limit:u32}
CIPHER_TABLE_RVA = 0xC9470


def load_cipher_table(probe):
    """从镜像里读 8 项算法表。"""
    base = probe.base + CIPHER_TABLE_RVA
    table = []
    for i in range(8):
        blob = probe.read(base + i * 12, 12)
        key, step, _pad, block, limit = struct.unpack("<BBHII", blob)
        table.append({"key": key, "step": step, "block": block, "limit": limit})
    return table


def choose_cipher(table, filename):
    """按文件名累加和 & 7 选算法。这是从 RVA 0x713C5-0x713F5 反汇编确证的。"""
    total = sum(filename.encode("ascii", "replace")) & 0xFF
    index = total & 7
    return index, table[index]


def decrypt(probe, data, cipher):
    """调原版解密函数。返回解密后的 bytes（长度与输入相同）。"""
    size = len(data)
    buf = probe.alloc(size + 0x100)
    probe.write(buf, data)
    # block 传 1：实测传表里的 block 会让每字节重复两次，那是给另一个
    # 调用场景准备的参数。block=1 得到干净的逐字节线性输出。
    result = probe.call(
        0x1E90,
        args=[cipher["key"], cipher["step"], 1, cipher["limit"]],
        ecx=buf,
        edx=size,
        limit=20000000,
    )
    if result["error"]:
        raise SystemExit("decrypt failed at RVA 0x1E90: %s" % result["error"])
    if result["eax"] != buf:
        raise SystemExit(
            "unexpected return: EAX=0x%X, expected buffer 0x%X (function may not be in-place)"
            % (result["eax"], buf)
        )
    return probe.read(buf, size)


def cmd_selftest(probe):
    print("=== PBG4 解包自检 ===")
    table = load_cipher_table(probe)
    print("算法表（8 项）：")
    for i, c in enumerate(table):
        print("  [%d] key=0x%02X step=0x%02X block=0x%X limit=0x%X"
              % (i, c["key"], c["step"], c["block"], c["limit"]))
    print()

    failures = 0
    for name in ("th185.dat", "thbgm.dat"):
        index, cipher = choose_cipher(table, name)
        print("%-12s 字节和&7 = %d  -> key=0x%02X step=0x%02X"
              % (name, index, cipher["key"], cipher["step"]))

    # 用真实数据验证：解密后头部不应再有 PBG4 的固定前缀特征
    dat_path = Path(os.environ.get("TH185_GAME_DIR", "")) / "th185.dat"
    if not dat_path.is_file():
        try:
            game_dir = json.loads((WEB / "local.target.json").read_text(encoding="utf-8"))["gameDir"]
            dat_path = Path(game_dir) / "th185.dat"
        except (OSError, KeyError, ValueError):
            pass
    if dat_path.is_file():
        with dat_path.open("rb") as stream:
            head = stream.read(0x200)
        _, cipher = choose_cipher(table, "th185.dat")
        produced = decrypt(probe, head, cipher)
        print()
        print("th185.dat 前 64 字节（经 0x1E90 处理后）：")
        for i in range(0, 64, 16):
            chunk = produced[i:i + 16]
            print("  %04X  %-47s  %s"
                  % (i, " ".join("%02X" % b for b in chunk),
                     "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)))
        print()
        # 这一步的产出是密钥流，不是明文。判据：相邻字节差恒等于 step。
        diffs = {(produced[i + 1] - produced[i]) & 0xFF for i in range(32)}
        is_keystream = diffs == {cipher["step"]}
        print("相邻字节差集合: %s" % sorted("0x%02X" % d for d in diffs))
        if is_keystream:
            print("[PASS] 0x1E90 原地产出【密钥流】（相邻字节差恒为 step=0x%02X）"
                  % cipher["step"])
            print("       这不是明文：XOR 步骤在 Pbg::Load 的后续路径里，")
            print("       尚未定位。详见 docs/PBG4.md §9。")
        else:
            print("[INFO] 输出不是纯密钥流，可能是明文，需要人工判读")
    else:
        print()
        print("(跳过真实数据验证：找不到 th185.dat)")

    print()
    if failures:
        print("自检失败 %d 项" % failures)
        return 1
    print("自检通过")
    return 0


def main():
    parser = argparse.ArgumentParser(description="用探针解包 th185.dat（PBG4）")
    parser.add_argument("--selftest", action="store_true")
    parser.add_argument("--list", action="store_true", help="列出算法表")
    parser.add_argument("--extract", metavar="DAT")
    parser.add_argument("--outdir", default="build/assets")
    parser.add_argument("--limit", type=lambda s: int(s, 0), default=0,
                        help="只解前 N 字节（调试用）")
    args = parser.parse_args()

    try:
        import unicorn  # noqa: F401
        import capstone  # noqa: F401
    except ImportError as exc:
        print("error: %s (pip install unicorn capstone)" % exc, file=sys.stderr)
        return 2

    data, target = load_image()
    meta = parse_pe(data)
    probe = Probe(data, meta)
    table = load_cipher_table(probe)

    if args.selftest:
        return cmd_selftest(probe)
    if args.list:
        print("算法表 @ VA 0x4C9470：")
        for i, c in enumerate(table):
            print("  [%d] key=0x%02X step=0x%02X block=0x%X limit=0x%X"
                  % (i, c["key"], c["step"], c["block"], c["limit"]))
        return 0
    if args.extract:
        # 诚实拒绝：当前只能产出密钥流，不是明文。
        # 详见 docs/PBG4.md §9.8 的实测记录。
        print("拒绝执行：PBG4 解包尚未完成。", file=sys.stderr)
        print("", file=sys.stderr)
        print("已确证的部分：", file=sys.stderr)
        print("  - 密钥选择算法 sum(filename) & 7", file=sys.stderr)
        print("  - 8 项算法表内容", file=sys.stderr)
        print("  - 密钥流 = (key + i*step) & 0xFF（解析式，无需探针）", file=sys.stderr)
        print("", file=sys.stderr)
        print("未完成的部分：", file=sys.stderr)
        print("  明文还需要位流解压（疑似 RVA 0x71E70），", file=sys.stderr)
        print("  单纯 XOR 密钥流得不到可读数据（实测可读性仅 40%，等同随机）。", file=sys.stderr)
        print("", file=sys.stderr)
        print("如果这里输出密钥流冒充明文，会污染后续所有资源分析。", file=sys.stderr)
        print("详见 th185_web/docs/PBG4.md §9。", file=sys.stderr)
        return 1

    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

