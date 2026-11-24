#!/usr/bin/env python3
r"""G2+ 的验证用例集：每个函数一个 verify_* 函数。

拆成独立文件是为了避免与 check_behavior.py 的引号嵌套。
每个 verify_* 返回 list[dict]，每项含 case / status，失败时附 binary 与 expected。
"""

import math
import struct


def _f2u(v):
    """float -> 32 位无符号（用于压栈）。"""
    return struct.unpack("<I", struct.pack("<f", v))[0]


def _u2f(b, off):
    """从字节串读 float。"""
    return struct.unpack_from("<f", b, off)[0]


def verify_polar_to_vec2(Runner, HEAP_BASE, sections, image_base):
    """0x00404B70（17 个同形状）：极坐标 -> Vec2（圆）。

        fld  dword [ebp+8]      ; angle
        fsincos                 ; st0=sin, st1=cos
        fmul dword [ebp+0xC]    ; * length
        fstp dword [eax]        ; x = sin*length
        fmul dword [ebp+0xC]    ; * length   <- 同一个参数
        fstp dword [eax+4]      ; y = cos*length

    x 用 sin、y 用 cos —— 不是常见的 (cos,sin)。必须用执行验证。
    """
    FUNC = 0x00404B70
    out = []
    # ★ 必须包含 sin != cos 的角度，否则无法区分 x=sin 与 x=cos
    for angle, length in [(0.0, 2.0), (0.7853981634, 1.0),
                          (1.5707963268, 3.0), (3.1415926536, 1.0),
                          (-0.5, 2.5), (0.3, 1.7), (2.0, 4.0)]:
        obj = bytearray(8)
        r = Runner(sections, image_base)
        eax, after, err = r.run_leaf(FUNC, HEAP_BASE + 0x1000, obj,
                                     args=[_f2u(angle), _f2u(length)])
        name = "a=%.4f L=%.1f" % (angle, length)
        if err:
            out.append({"case": name, "status": "ERROR", "error": err})
            continue
        gx, gy = _u2f(after, 0), _u2f(after, 4)
        # ★ 实测修正：x 用 cos、y 用 sin（我最初写反了）
        ex, ey = math.cos(angle) * length, math.sin(angle) * length
        ok = abs(gx - ex) < 1e-5 and abs(gy - ey) < 1e-5
        e = {"case": name, "status": "PASS" if ok else "FAIL"}
        if not ok:
            e["binary"] = {"x": round(gx, 6), "y": round(gy, 6)}
            e["expected"] = {"x": round(ex, 6), "y": round(ey, 6)}
        out.append(e)
    return out


def verify_polar_scaled(Runner, HEAP_BASE, sections, image_base):
    """0x00417750（2 个）：极坐标 -> Vec2，x/y 用不同 length。

    与 0x00404B70 只差一处：第二次 fmul 用 [ebp+0x10] 而非 [ebp+0xC]。
    """
    FUNC = 0x00417750
    out = []
    for angle, lx, ly in [(0.5, 2.0, 3.0), (1.0, 1.0, 5.0), (0.0, 4.0, 4.0)]:
        obj = bytearray(8)
        r = Runner(sections, image_base)
        eax, after, err = r.run_leaf(FUNC, HEAP_BASE + 0x1000, obj,
                                     args=[_f2u(angle), _f2u(lx), _f2u(ly)])
        name = "a=%.2f lx=%.1f ly=%.1f" % (angle, lx, ly)
        if err:
            out.append({"case": name, "status": "ERROR", "error": err})
            continue
        gx, gy = _u2f(after, 0), _u2f(after, 4)
        # ★ 实测修正：x 用 cos*lx、y 用 sin*ly
        ex, ey = math.cos(angle) * lx, math.sin(angle) * ly
        ok = abs(gx - ex) < 1e-5 and abs(gy - ey) < 1e-5
        e = {"case": name, "status": "PASS" if ok else "FAIL"}
        if not ok:
            e["binary"] = {"x": round(gx, 6), "y": round(gy, 6)}
            e["expected"] = {"x": round(ex, 6), "y": round(ey, 6)}
        out.append(e)
    return out


def verify_copy_vec3(Runner, HEAP_BASE, sections, image_base):
    """0x00405830（3 个）：拷贝 12 字节（Vec3）。"""
    FUNC = 0x00405830
    src_data = bytes(range(0x11, 0x11 + 12))
    obj = bytearray(b"\xEE" * 12)
    src_va = HEAP_BASE + 0x2000
    r = Runner(sections, image_base)
    r.uc.mem_write(src_va, src_data)
    eax, after, err = r.run_leaf(FUNC, HEAP_BASE + 0x1000, obj, args=[src_va])
    if err:
        return [{"case": "copy 12 bytes", "status": "ERROR", "error": err}]
    ok = (after == src_data)
    e = {"case": "copy 12 bytes", "status": "PASS" if ok else "FAIL"}
    if not ok:
        e["binary"] = after.hex()
        e["expected"] = src_data.hex()
    return [e]


def verify_set_first_field(Runner, HEAP_BASE, sections, image_base):
    """0x00406720（3 个）：写首字段。"""
    FUNC = 0x00406720
    out = []
    for val in (0, 0x12345678, 0xFFFFFFFF):
        obj = bytearray(b"\xAA" * 8)
        r = Runner(sections, image_base)
        eax, after, err = r.run_leaf(FUNC, HEAP_BASE + 0x1000, obj, args=[val])
        name = "set 0x%08X" % val
        if err:
            out.append({"case": name, "status": "ERROR", "error": err})
            continue
        got = struct.unpack_from("<I", after, 0)[0]
        tail_ok = after[4:] == b"\xAA" * 4
        ok = (got == val and tail_ok)
        e = {"case": name, "status": "PASS" if ok else "FAIL"}
        if not ok:
            e["binary"] = "0x%08X tail=%s" % (got, after[4:].hex())
            e["expected"] = "0x%08X tail=aaaaaaaa" % val
        out.append(e)
    return out


def verify_timer_tick(Runner, HEAP_BASE, sections, image_base):
    """0x00402A50（0x00402A50..0x00402A93，68 字节）：倒计时状态机。

        countdown = *(int*)(this + 0x8C) - 1
        *(int*)(this + 0x8C) = countdown
        if countdown < 0:
            *(int*)(this + 0x8C) = 0
            *(int*)(this + 0x00) = 0
            *(int*)(this + 0xD4) = 0
            return
        *(int*)(this + 0x00) = *(int*)(this + 0x0C + countdown*4)
        *(int*)(this + 0x08) = *(int*)(this + 0x4C + countdown*4)
        *(int*)(this + 0xD4) = 0
    """
    FUNC = 0x00402A50
    OBJ = 0xD8
    out = []

    def make_obj(init_cd):
        o = bytearray(OBJ)
        struct.pack_into("<i", o, 0x8C, init_cd)
        struct.pack_into("<I", o, 0x00, 0xDEADBEEF)
        struct.pack_into("<I", o, 0x08, 0xFEEDFACE)
        struct.pack_into("<I", o, 0xD4, 0xCAFEBABE)
        for k in range(16):
            struct.pack_into("<i", o, 0x0C + k * 4, 0xA000 + k)
            struct.pack_into("<i", o, 0x4C + k * 4, 0xB000 + k)
        return o

    for init_cd, desc in [(5,  "non-negative: 5-1=4 -> A[4]/B[4]"),
                          (1,  "boundary: 1-1=0 -> A[0]/B[0]"),
                          (0,  "boundary: 0-1=-1 -> clear"),
                          (-1, "negative input"),
                          (15, "15-1=14 (last array index)"),
                          (16, "16-1=15 (array tail)"),
                          (30, "30-1=29 (beyond array)")]:
        obj = make_obj(init_cd)
        r = Runner(sections, image_base)
        eax, after, err = r.run_leaf(FUNC, HEAP_BASE + 0x1000, obj)
        if err:
            out.append({"case": desc, "status": "ERROR", "error": err})
            continue
        exp = bytearray(obj)
        cd = init_cd - 1
        if cd < 0:
            struct.pack_into("<i", exp, 0x8C, 0)
            struct.pack_into("<I", exp, 0x00, 0)
            struct.pack_into("<I", exp, 0xD4, 0)
        else:
            struct.pack_into("<i", exp, 0x8C, cd)
            ia, ib2 = 0x0C + cd * 4, 0x4C + cd * 4
            va = struct.unpack_from('<i', obj, ia)[0] if ia + 4 <= OBJ else 0
            vb = struct.unpack_from('<i', obj, ib2)[0] if ib2 + 4 <= OBJ else 0
            struct.pack_into("<i", exp, 0x00, va)
            struct.pack_into("<i", exp, 0x08, vb)
            struct.pack_into("<I", exp, 0xD4, 0)
        expected = bytes(exp)
        ok = (after == expected)
        e = {"case": desc, "status": "PASS" if ok else "FAIL"}
        if not ok:
            for off in (0x00, 0x08, 0x8C, 0xD4):
                if after[off:off+4] != expected[off:off+4]:
                    e["binary"] = {"offset": "0x%02X" % off,
                                   "value": "0x%08X" % struct.unpack("<I", after[off:off+4])[0]}
                    e["expected"] = {"offset": "0x%02X" % off,
                                     "value": "0x%08X" % struct.unpack("<I", expected[off:off+4])[0]}
                    break
        out.append(e)
    return out

# 注册表：(名称, 函数地址, 验证器)
CASES = [
    ("0x00402A50 timer tick", 0x00402A50, "verify_timer_tick"),
    ("0x00404B70 polar (circle)", 0x00404B70, "verify_polar_to_vec2"),
    ("0x00417750 polar (scaled)", 0x00417750, "verify_polar_scaled"),
    ("0x00405830 copy_vec3", 0x00405830, "verify_copy_vec3"),
    ("0x00406720 set_first_field", 0x00406720, "verify_set_first_field"),
]