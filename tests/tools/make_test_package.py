#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""构造 DPKViewer 交叉验证所需的测试数据。

用法: python make_test_package.py <workdir>

生成内容（全部由参考实现 dpk.py 的编解码函数产出，作为 C++ 实现的对照基准）：
  src/                  原始文件树（含多块、空文件、块边界、中文与空格文件名）
  manifest.txt          期望清单：md5 <TAB> size <TAB> relpath(UTF-8, '/' 分隔)
  packages/ref_flags3.dpk   flags=3（压缩 + 加密）
  packages/ref_flags1.dpk   flags=1（仅压缩）
  packages/ref_flags0.dpk   flags=0（不压缩不加密）
  des_fixtures.txt      DES 标准块向量(ECB) 与 DPK 变体向量(VAR)，由 pycryptodome / dpk.py 生成
  zlib_fixtures/        <name>.bin 原文 与 <name>.z（Python zlib 压缩结果）
"""

import hashlib
import os
import random
import shutil
import struct
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "reference"))
import dpk as ref  # noqa: E402  参考实现

BLOCK = ref.BLOCK
PKG_PWD = ref.PKG_PWD
CT_PWD = ref.CT_PWD


def name_bytes(name):
    """包内路径名编码：模拟真实包使用系统 ANSI（GBK）字节。"""
    try:
        return name.encode("gbk")
    except UnicodeEncodeError:
        return name.encode("utf-8")


def build_source_tree(seed=20261023):
    rnd = random.Random(seed)
    files = {}

    def text(s):
        return s.encode("utf-8")

    files["readme.txt"] = text(
        "DPKViewer test package\r\n"
        "line2: 中文内容用于验证 GBK/UTF-8 名称与内容编码\r\n"
    )
    files["cfg/server.ini"] = text("[server]\nhost=127.0.0.1\nport=8888\nname=测试服\n")
    files["data/empty.bin"] = b""
    files["data/one.bin"] = b"A"
    # 虚拟文件块边界：单块容量 992 字节；跨块后每块 1016 字节
    files["data/size_007.bin"] = bytes(rnd.getrandbits(8) for _ in range(7))
    files["data/size_008.bin"] = bytes(rnd.getrandbits(8) for _ in range(8))
    files["data/size_991.bin"] = bytes(rnd.getrandbits(8) for _ in range(991))
    files["data/size_992.bin"] = bytes(rnd.getrandbits(8) for _ in range(992))
    files["data/size_993.bin"] = bytes(rnd.getrandbits(8) for _ in range(993))
    files["data/size_1016.bin"] = bytes(rnd.getrandbits(8) for _ in range(1016))
    files["data/size_1017.bin"] = bytes(rnd.getrandbits(8) for _ in range(1017))
    files["data/size_3000.bin"] = bytes(rnd.getrandbits(8) for _ in range(3000))
    files["data/highly_compressible.bin"] = b"\x00" * 6000
    files["data/text_repeat.bin"] = (b"whpackage1.0-" * 400)
    files["数据/中文目录/中文名.txt"] = text("中文路径与内容的测试文件\n")
    files["数据/中文目录/with space and.dot.txt"] = text("space in name\n")
    files["scripts/start.bat"] = text("@echo off\r\necho hello\r\n")
    return files


def build_package(entries, flags, template_header=None):
    """按参考实现的读写约定生成 DPK 包字节。

    entries: [(relpath, data)]；relpath 使用 '/' 分隔。
    """
    prepared = []
    cursor = 1
    for path, data in entries:
        stream = ref.encode_whsc(data, flags=flags)
        blocks = ref.blocks_for(len(stream))
        prepared.append((path, stream, cursor, blocks))
        cursor += blocks

    names_blob = bytearray()
    name_offsets = {}

    def offset_of(name):
        raw = name_bytes(name)
        if raw in name_offsets:
            return name_offsets[raw]
        off = len(names_blob)
        names_blob.extend(raw)
        names_blob.append(0)
        name_offsets[raw] = off
        return off

    tree = bytearray()
    dir_stack = []
    for path, stream, root, blocks in prepared:
        comps = path.split("/")
        dirs, fname = comps[:-1], comps[-1]
        common = 0
        while common < len(dirs) and common < len(dir_stack) and dirs[common] == dir_stack[common]:
            common += 1
        while len(dir_stack) > common:
            tree.append(3)
            dir_stack.pop()
        for d in dirs[common:]:
            tree.append(2)
            tree.extend(struct.pack("<I", offset_of(d)))
            dir_stack.append(d)
        tree.append(1)
        tree.extend(struct.pack("<I", offset_of(fname)))
        tree.extend(struct.pack("<I", root))

    tree_stream = ref.encode_whsc(bytes(tree), flags=flags)
    names_stream = ref.encode_whsc(bytes(names_blob), flags=flags)
    tree_root = cursor
    cursor += ref.blocks_for(len(tree_stream))
    names_root = cursor
    cursor += ref.blocks_for(len(names_stream))

    header = bytearray(template_header or BLOCK)
    header[0:12] = b"whpackage1.0"
    header[0x20:0x30] = hashlib.md5(PKG_PWD).digest()
    struct.pack_into("<2I", header, 0x84, tree_root, names_root)

    out = bytearray(header)

    def append_vfile(stream, root):
        blocks = ref.blocks_for(len(stream))
        root_blk = bytearray(BLOCK)
        struct.pack_into("<4I", root_blk, 0, 0, (root + 1) if blocks > 1 else 0, root + blocks - 1, len(stream))
        take = min(len(stream), BLOCK - 32)
        root_blk[32:32 + take] = stream[:take]
        out.extend(root_blk)
        off = take
        for i in range(1, blocks):
            blk = bytearray(BLOCK)
            struct.pack_into("<2I", blk, 0, root + i - 1, (root + i + 1) if (i + 1) < blocks else 0)
            cnt = min(len(stream) - off, BLOCK - 8)
            blk[8:8 + cnt] = stream[off:off + cnt]
            off += cnt
            out.extend(blk)

    for path, stream, root, blocks in prepared:
        append_vfile(stream, root)
    append_vfile(tree_stream, tree_root)
    append_vfile(names_stream, names_root)
    while len(out) % BLOCK:
        out.append(0)
    assert len(out) // BLOCK == cursor, "块布局与预期不一致"
    return bytes(out)


def write_des_fixtures(path, seed=424242):
    from Crypto.Cipher import DES as PYDES

    rnd = random.Random(seed)
    lines = []
    for _ in range(20):
        key = bytes(rnd.getrandbits(8) for _ in range(8))
        plain = bytes(rnd.getrandbits(8) for _ in range(8))
        cipher = PYDES.new(key, PYDES.MODE_ECB).encrypt(plain)
        lines.append("ECB %s %s %s" % (key.hex(), plain.hex(), cipher.hex()))
    sizes = [1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 15, 16, 17, 24, 31, 56, 100, 993]
    for size in sizes:
        key = bytes(rnd.getrandbits(8) for _ in range(8))
        plain = bytes(rnd.getrandbits(8) for _ in range(size))
        cipher = ref._encrypt(plain, key)
        decback = ref.Dpk._decrypt(cipher, key)
        lines.append("VAR %s %s %s %s" % (key.hex(), plain.hex(), cipher.hex(), decback.hex()))
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    if any(len(line.split()) == 5 and line.split()[2] != line.split()[4] for line in lines):
        raise AssertionError("参考实现的 DES 变体在部分长度上不可逆，需在文档中标注")
    return len(lines)


def write_zlib_fixtures(dirpath, seed=777):
    rnd = random.Random(seed)
    if os.path.isdir(dirpath):
        shutil.rmtree(dirpath)
    os.makedirs(dirpath)
    shapes = {
        "empty": b"",
        "one": b"z",
        "zeros": b"\x00" * 5000,
        "random3000": bytes(rnd.getrandbits(8) for _ in range(3000)),
        "rand700": bytes(rnd.getrandbits(8) for _ in range(700)),
        "text": (b"the quick brown fox jumps over the lazy dog. " * 200),
        "mixed": bytes(rnd.getrandbits(8) for _ in range(50)) + b"\x00" * 4000 + b"tail" * 20,
        "utf8text": ("中文压缩测试 " * 500).encode("utf-8"),
    }
    count = 0
    for name, data in shapes.items():
        with open(os.path.join(dirpath, name + ".bin"), "wb") as f:
            f.write(data)
        for level in (0, 1, 6, 9):
            packed = zlib.compress(data, level)
            with open(os.path.join(dirpath, "%s.l%d.z" % (name, level)), "wb") as f:
                f.write(packed)
            count += 1
        # 另外用 compressobj 输出固定 Huffman 尝试（策略默认即可）
    return count


def main():
    if len(sys.argv) < 2:
        print("用法: python make_test_package.py <workdir>")
        return 2
    work = os.path.abspath(sys.argv[1])
    src_dir = os.path.join(work, "src")
    if os.path.isdir(src_dir):
        shutil.rmtree(src_dir)
    files = build_source_tree()

    manifest = []
    for rel, data in sorted(files.items()):
        dst = os.path.join(src_dir, *rel.split("/"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(data)
        manifest.append((hashlib.md5(data).hexdigest(), len(data), rel))

    with open(os.path.join(work, "manifest.txt"), "w", encoding="utf-8", newline="\n") as f:
        for md5hex, size, rel in manifest:
            f.write("%s\t%d\t%s\n" % (md5hex, size, rel))

    pkg_dir = os.path.join(work, "packages")
    if os.path.isdir(pkg_dir):
        shutil.rmtree(pkg_dir)
    os.makedirs(pkg_dir)
    entries = sorted(files.items())
    for flags, name in ((3, "ref_flags3.dpk"), (1, "ref_flags1.dpk"), (0, "ref_flags0.dpk")):
        blob = build_package(entries, flags)
        with open(os.path.join(pkg_dir, name), "wb") as f:
            f.write(blob)
        print("写入 %-16s flags=%d 大小=%d 块数=%d" % (name, flags, len(blob), len(blob) // BLOCK))

    n_des = write_des_fixtures(os.path.join(work, "des_fixtures.txt"))
    n_z = write_zlib_fixtures(os.path.join(work, "zlib_fixtures"))

    print("源文件 %d 个，manifest %d 条，DES 向量 %d 条，zlib 样本 %d 个" % (len(files), len(manifest), n_des, n_z))
    print("MAKE-TEST-DATA: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())