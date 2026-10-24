#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用参考实现 dpk.py 的编解码原语复核 DPKViewer(C++) 的产物。

用法: python verify.py <workdir>   （在 TestCore.exe 跑完之后执行）

检查项：
  A. out/ref_flags*.dpk/      C++ 解包参考包的结果与 manifest.txt 完全一致
  B. packed/*.dpk             C++ 生成的包可被参考实现完整解开，内容与 manifest 一致，
                              且 whsc flags 与打包选项相符
  C. zlib_fixtures/*.cppz*    C++ 自实现 deflate 的输出可被 Python zlib 解压还原
"""

import hashlib
import os
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "reference"))
import dpk as ref  # noqa: E402

problems = []
checks = 0


def fail(msg):
    problems.append(msg)
    print("[FAIL] %s" % msg)


def check(ok, msg):
    global checks
    checks += 1
    if ok:
        print("[ ok ] %s" % msg)
    else:
        fail(msg)


def load_manifest(work):
    items = {}
    order = []
    with open(os.path.join(work, "manifest.txt"), encoding="utf-8") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            if len(parts) != 3:
                continue
            items[parts[2]] = (parts[0], int(parts[1]))
            order.append(parts[2])
    return items, order


def name_decode(raw):
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("gbk", "replace")


def reference_dump(path):
    """用参考实现原语解出包内全部条目：{path: (plain, flags)}"""
    d = ref.Dpk(path)
    hdr = d._read_at(0, ref.BLOCK)
    if hdr[:12] != b"whpackage1.0":
        raise ValueError("不是 whpackage1.0 包: %s" % path)
    tree_root, names_root = struct.unpack_from("<2I", hdr, 0x84)
    tree = d._decode_whsc(d._vfile(tree_root))
    names_blob = d._decode_whsc(d._vfile(names_root))

    name_of = {}
    i = 0
    while i < len(names_blob):
        j = names_blob.find(b"\0", i)
        if j < 0:
            raise ValueError("路径表损坏")
        name_of[i] = name_decode(names_blob[i:j])
        i = j + 1

    out = {}
    dirs = []
    off = 0
    while off < len(tree):
        t = tree[off]
        off += 1
        if t == 1:
            noff, blk = struct.unpack_from("<2I", tree, off)
            off += 8
            nm = name_of.get(noff)
            if nm is None:
                raise ValueError("名称偏移无效 %d" % noff)
            stream = d._vfile(blk)
            out["/".join(dirs + [nm])] = (d._decode_whsc(stream), stream[15])
        elif t == 2:
            noff = struct.unpack_from("<I", tree, off)[0]
            off += 4
            dirs.append(name_of.get(noff, "?"))
        elif t == 3:
            dirs.pop()
        else:
            raise ValueError("未知目录记录 %d" % t)
    d.close()
    return out


def verify_extracted(work, manifest, pkg_name):
    root = os.path.join(work, "out", pkg_name)
    if not os.path.isdir(root):
        fail("缺少解包结果目录 %s" % root)
        return
    found = {}
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root).replace("\\", "/")
            with open(full, "rb") as f:
                data = f.read()
            found[rel] = (hashlib.md5(data).hexdigest(), len(data))
    missing = sorted(set(manifest) - set(found))
    extra = sorted(set(found) - set(manifest))
    if missing:
        fail("%s 缺少条目: %s" % (pkg_name, missing[:5]))
    if extra:
        fail("%s 多出条目: %s" % (pkg_name, extra[:5]))
    bad = []
    for rel, (md5hex, size) in manifest.items():
        got = found.get(rel)
        if got != (md5hex, size):
            bad.append(rel)
    check(not (missing or extra or bad),
          "C++ 解包 %s 与 manifest 一致（%d 条）" % (pkg_name, len(manifest)))


def verify_packed(work, manifest, packed_name, expect_flags):
    path = os.path.join(work, "packed", packed_name)
    if not os.path.isfile(path):
        fail("缺少打包产物 %s" % path)
        return
    try:
        got = reference_dump(path)
    except Exception as exc:  # noqa: BLE001
        fail("参考实现无法解析 %s: %s" % (packed_name, exc))
        return
    missing = sorted(set(manifest) - set(got))
    extra = sorted(set(got) - set(manifest))
    if missing:
        fail("%s 缺少条目: %s" % (packed_name, missing[:5]))
    if extra:
        fail("%s 多出条目: %s" % (packed_name, extra[:5]))
    bad = []
    badflags = []
    for rel, (md5hex, size) in manifest.items():
        if rel not in got:
            continue
        plain, flags = got[rel]
        if len(plain) != size or hashlib.md5(plain).hexdigest() != md5hex:
            bad.append(rel)
        if flags != expect_flags:
            badflags.append("%s(flags=%d)" % (rel, flags))
    if bad:
        fail("%s 内容不符: %s" % (packed_name, bad[:5]))
    if badflags:
        fail("%s flags 期望 %d，实际 %s" % (packed_name, expect_flags, badflags[:5]))
    check(not (missing or extra or bad or badflags),
          "参考实现解开 %s 与 manifest 一致，flags=%d（%d 条）" % (packed_name, expect_flags, len(got)))


def verify_cppz(work):
    d = os.path.join(work, "zlib_fixtures")
    if not os.path.isdir(d):
        fail("缺少 zlib_fixtures 目录")
        return
    total = 0
    bad = []
    for fn in sorted(os.listdir(d)):
        if ".cppz" not in fn:
            continue
        base = fn.split(".cppz")[0]
        level = fn.split(".cppz")[1]
        with open(os.path.join(d, base + ".bin"), "rb") as f:
            expect = f.read()
        with open(os.path.join(d, fn), "rb") as f:
            packed = f.read()
        try:
            got = zlib.decompress(packed)
        except Exception as exc:  # noqa: BLE001
            bad.append("%s: %s" % (fn, exc))
            continue
        if got != expect:
            bad.append("%s 内容不一致" % fn)
        total += 1
    if bad:
        fail("C++ 压缩输出无法被 Python zlib 还原: %s" % bad[:5])
    check(total >= 16 and not bad, "Python zlib 可还原 C++ 压缩输出（%d 个，level 0/9）" % total)


def main():
    if len(sys.argv) < 2:
        print("用法: python verify.py <workdir>")
        return 2
    work = os.path.abspath(sys.argv[1])
    manifest, order = load_manifest(work)
    check(bool(manifest), "载入 manifest.txt（%d 条）" % len(manifest))

    for name in ("ref_flags3.dpk", "ref_flags1.dpk", "ref_flags0.dpk"):
        verify_extracted(work, manifest, name)

    verify_packed(work, manifest, "pack_ce.dpk", 3)
    verify_packed(work, manifest, "pack_ce_stored.dpk", 3)
    verify_packed(work, manifest, "pack_c.dpk", 1)
    verify_packed(work, manifest, "pack_e.dpk", 2)
    verify_packed(work, manifest, "pack_raw.dpk", 0)
    verify_packed(work, manifest, "pack_ce_template.dpk", 3)

    verify_cppz(work)

    print("\n检查 %d 项，问题 %d 项" % (checks, len(problems)))
    print("%s" % ("VERIFY: PASS" if not problems else "VERIFY: FAIL"))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())