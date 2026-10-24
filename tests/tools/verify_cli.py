#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用参考实现 dpk.py 复核 DPKViewer.exe 命令行模式的端到端产物。

用法: python verify_cli.py <workdir>   （在 tests\\tools\\cli_smoke.bat 之后执行）

检查项:
  A. cli_out/               /extract 导出的目录与 manifest.txt 完全一致（条目/大小/MD5）
  B. cli_pack.dpk           /pack（压缩+加密+沿用模板包头）的产物可被参考实现完整解开，
                            flags=3，且 1024 字节包头除目录树/路径表块号外与模板一致
  C. cli_pack_plain.dpk     /pack（不压缩不加密）的产物可被参考实现完整解开，flags=0，
                            包头含 whpackage1.0 魔数与包口令 MD5，块号在总块数范围内
  D. log_cli_*.txt          CLI 日志内容符合预期（条目数、成功/失败计数、回读校验）
"""

import hashlib
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import verify as V  # noqa: E402  （复用其 reference_dump / load_manifest / manifest 比对逻辑）

PACKAGE_MAGIC = b"whpackage1.0"
PACKAGE_PASSWORD = b"tianxiawudi"
BLOCK = 1024
HEADER_TREE_ROOT = 0x84
HEADER_NAMES_ROOT = 0x88

problems = []
checks = 0


def check(ok, msg):
    global checks
    checks += 1
    if ok:
        print("[ ok ] %s" % msg)
    else:
        problems.append(msg)
        print("[FAIL] %s" % msg)


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def compare_tree(root, manifest, label):
    if not os.path.isdir(root):
        check(False, "%s: 目录不存在 %s" % (label, root))
        return
    found = {}
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            rel = os.path.relpath(full, root).replace("\\", "/")
            data = read_bytes(full)
            found[rel] = (hashlib.md5(data).hexdigest(), len(data))
    missing = sorted(set(manifest) - set(found))
    extra = sorted(set(found) - set(manifest))
    bad = [rel for rel, want in manifest.items() if rel in found and found[rel] != want]
    if missing:
        print("       缺少: %s" % missing[:5])
    if extra:
        print("       多出: %s" % extra[:5])
    if bad:
        print("       内容不符: %s" % bad[:5])
    check(not (missing or extra or bad),
          "%s: 与 manifest 一致（%d 条，大小与 MD5 全部命中）" % (label, len(manifest)))


def compare_package(path, manifest, label, expect_flags):
    if not os.path.isfile(path):
        check(False, "%s: 缺少产物 %s" % (label, path))
        return None
    try:
        got = V.reference_dump(path)
    except Exception as exc:  # noqa: BLE001
        check(False, "%s: 参考实现无法解析: %s" % (label, exc))
        return None
    missing = sorted(set(manifest) - set(got))
    extra = sorted(set(got) - set(manifest))
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
    if missing:
        print("       缺少条目: %s" % missing[:5])
    if extra:
        print("       多出条目: %s" % extra[:5])
    if bad:
        print("       内容不符: %s" % bad[:5])
    if badflags:
        print("       flags 不符: %s" % badflags[:5])
    check(not (missing or extra or bad or badflags),
          "%s: 参考实现解开与 manifest 一致，flags=%d（%d 条）" % (label, expect_flags, len(got)))
    return read_bytes(path)


def check_header(pkg_bytes, label, template=None):
    hdr = pkg_bytes[:BLOCK]
    check(hdr[:12] == PACKAGE_MAGIC, "%s: 包头魔数为 whpackage1.0" % label)
    check(hdr[0x20:0x30] == hashlib.md5(PACKAGE_PASSWORD).digest(),
          "%s: 包头 0x20 处为包口令 MD5(tianxiawudi)" % label)
    total_blocks = len(pkg_bytes) // BLOCK
    tree_root, names_root = struct.unpack_from("<2I", hdr, HEADER_TREE_ROOT)
    check(0 < tree_root < total_blocks and 0 < names_root < total_blocks and tree_root != names_root,
          "%s: 目录树/路径表块号在总块数范围内（tree=%d names=%d total=%d）"
          % (label, tree_root, names_root, total_blocks))
    if template is None:
        return
    same_body = (hdr[:HEADER_TREE_ROOT] == template[:HEADER_TREE_ROOT] and
                 hdr[HEADER_NAMES_ROOT + 4:BLOCK] == template[HEADER_NAMES_ROOT + 4:BLOCK])
    check(same_body, "%s: 除目录树/路径表块号外，包头与模板包逐字节一致" % label)


def check_logs(work):
    list_log = read_bytes(os.path.join(work, "log_cli_list.txt")).decode("utf-8-sig")
    check("条目 17 条" in list_log and "OK 共 17 条，其中头部异常 0 条" in list_log,
          "/list 日志：列出 17 条且无头部异常")

    extract_log = read_bytes(os.path.join(work, "log_cli_extract.txt")).decode("utf-8-sig")
    lines = [ln for ln in extract_log.splitlines() if ln.strip()]
    ok_lines = [ln for ln in lines if ln.startswith("  ok")]
    check(len(ok_lines) == 17 and "成功 17 条，失败 0 条" in extract_log,
          "/extract 日志：17 条成功、失败 0 条")

    pack_log = read_bytes(os.path.join(work, "log_cli_pack.txt")).decode("utf-8-sig")
    check("条目 17，跳过 0" in pack_log and "OK 回读校验: 条目 17 条" in pack_log and "模板包头=有" in pack_log,
          "/pack 日志：写入 17 条、跳过 0、模板包头=有，且回读校验 17 条")

    plain_log = read_bytes(os.path.join(work, "log_cli_pack_plain.txt")).decode("utf-8-sig")
    check("压缩=否 加密=否 模板包头=无" in plain_log and "OK 回读校验: 条目 17 条" in plain_log,
          "/pack(不压缩不加密) 日志：选项生效且回读校验 17 条")


def main():
    if len(sys.argv) < 2:
        print("用法: python verify_cli.py <workdir>")
        return 2
    work = os.path.abspath(sys.argv[1])
    manifest, _order = V.load_manifest(work)
    check(bool(manifest), "载入 manifest.txt（%d 条）" % len(manifest))

    compare_tree(os.path.join(work, "cli_out"), manifest, "cli_out（命令行解包）")

    template = read_bytes(os.path.join(work, "packages", "ref_flags3.dpk"))[:BLOCK]
    packed = compare_package(os.path.join(work, "cli_pack.dpk"), manifest,
                             "cli_pack.dpk（压缩+加密+模板包头）", 3)
    if packed is not None:
        check_header(packed, "cli_pack.dpk", template)

    plain = compare_package(os.path.join(work, "cli_pack_plain.dpk"), manifest,
                            "cli_pack_plain.dpk（不压缩不加密）", 0)
    if plain is not None:
        check_header(plain, "cli_pack_plain.dpk")

    check_logs(work)

    print("\n检查 %d 项，问题 %d 项" % (checks, len(problems)))
    print("VERIFY-CLI: %s" % ("PASS" if not problems else "FAIL"))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())