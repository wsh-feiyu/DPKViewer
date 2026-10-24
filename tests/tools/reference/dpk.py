# -*- coding: utf-8 -*-
"""寻仙 whpackage1.0 (.dpk) 读取器

格式（依据 XunxianDpkViewer 的 Core/DpkReader.cs 复刻）：

  整包 = N 个 1024 字节的块；块 0 是包头（含 whpackage1.0 魔数、
         0x20 处 = MD5("tianxiawudi")、0x84 = 目录树根块号、0x88 = 路径表根块号）
  虚拟文件 = 从 rootBlock 起的块链：
         根块头 32B：[prev:4][next:4][tail:4][fileSize:4] + 16B 保留，负载从 +32 开始
         后续块头 8B：[prev:4][next:4]，负载从 +8 开始
  文件内容 = whsc1.0 流：
         [0:7]="whsc1.0" [15]=flags [16:32]=MD5(明文) [32]=0 [36]=plainSize [40]=storedSize [48:]=payload
         flags&2 -> DES-ECB 解密（key = 密码前 8 字节；flags&4 用 ContentPassword 否则 PackagePassword）
         flags&1 -> zlib 解压
"""
import os
import struct
import hashlib
import zlib

try:
    from Crypto.Cipher import DES          # 有 pycryptodome 时更快
except ImportError:                        # 服务端没有就用纯 Python 实现
    import os as _os
    import sys as _sys
    _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
    import des as DES

BLOCK = 1024
PKG_PWD = b"tianxiawudi"
CT_PWD = b"gandiaosifu"


class Dpk:
    def __init__(self, path):
        self.path = path
        self.f = open(path, "rb")
        self.size = os.path.getsize(path)
        self._entries = None

    # ---------- 底层 ----------
    def _read_at(self, off, n):
        self.f.seek(off)
        b = self.f.read(n)
        if len(b) != n:
            raise EOFError("读取越界 off=%d n=%d" % (off, n))
        return b

    def _vfile(self, root_block):
        root = self._read_at(root_block * BLOCK, BLOCK)
        prev, nxt, tail, fsz = struct.unpack_from("<4I", root, 0)
        if prev != 0:
            raise ValueError("块 %d 不是根块" % root_block)
        out = bytearray(root[32:32 + min(fsz, BLOCK - 32)])
        cur, last, seen = nxt, root_block, {root_block}
        while cur != 0 and len(out) < fsz:
            if cur in seen:
                raise ValueError("块链成环 %d" % cur)
            seen.add(cur)
            blk = self._read_at(cur * BLOCK, BLOCK)
            bprev, bnxt = struct.unpack_from("<2I", blk, 0)
            if bprev != last:
                raise ValueError("块链断裂 %d->%d" % (last, cur))
            cnt = min(fsz - len(out), BLOCK - 8)
            out += blk[8:8 + cnt]
            last, cur = cur, bnxt
        if last != tail:
            raise ValueError("尾块校验失败")
        return bytes(out)

    @staticmethod
    def _decrypt(payload, key8):
        full = len(payload) // 8 * 8
        res = bytearray(len(payload))
        prev = b"\0" * 8
        if full:
            dec = DES.new(key8, DES.MODE_ECB).decrypt(payload[:full])
            for o in range(0, full, 8):
                for i in range(8):
                    res[o + i] = dec[o + i] ^ prev[i]
                prev = payload[o:o + 8]
        if full < len(payload):
            t = bytearray(payload[full:])
            parity = (t[-1] + t[0]) & 1
            for left in range(len(t) // 2):
                right = len(t) - left - 1
                if ((t[right] + t[left]) & 1) == parity:
                    t[left], t[right] = t[right], t[left]
            prior = 0
            for i in range(len(t)):
                v = t[i]
                t[i] = v ^ key8[i] ^ prior ^ prev[i]
                prior = v
                res[full + i] = t[i]
        return bytes(res)

    def _decode_whsc(self, data):
        if len(data) < 48 or data[:7] != b"whsc1.0":
            raise ValueError("不是 WHSC 流")
        flags = data[15]
        digest = data[16:32]
        plain = struct.unpack_from("<I", data, 36)[0]
        stored = struct.unpack_from("<I", data, 40)[0]
        payload = data[48:48 + stored]
        if flags & 2:
            src = CT_PWD if (flags & 4) else PKG_PWD
            payload = self._decrypt(payload, src[:8])
        if flags & 1:
            payload = zlib.decompress(payload)
        if len(payload) != plain:
            raise ValueError("长度不符 %d != %d" % (len(payload), plain))
        if hashlib.md5(payload).digest() != digest:
            raise ValueError("MD5 校验失败")
        return payload

    # ---------- 目录 ----------
    def entries(self):
        if self._entries is not None:
            return self._entries
        hdr = self._read_at(0, BLOCK)
        if hdr[:12] != b"whpackage1.0":
            raise ValueError("不是 whpackage1.0 包")
        tree_root, names_root = struct.unpack_from("<2I", hdr, 0x84)
        tree = self._decode_whsc(self._vfile(tree_root))
        names = self._decode_whsc(self._vfile(names_root))

        name_of = {}
        i = 0
        while i < len(names):
            j = names.find(b"\0", i)
            if j < 0:
                raise ValueError("路径表损坏")
            name_of[i] = names[i:j].decode("ascii", "replace")
            i = j + 1

        out, dirs, off = [], [], 0
        while off < len(tree):
            t = tree[off]; off += 1
            if t == 1:
                noff, blk = struct.unpack_from("<2I", tree, off); off += 8
                nm = name_of[noff]
                path = "/".join(dirs + [nm])
                out.append((path, blk))
            elif t == 2:
                noff = struct.unpack_from("<I", tree, off)[0]; off += 4
                dirs.append(name_of[noff])
            elif t == 3:
                dirs.pop()
            else:
                raise ValueError("未知目录记录 %d" % t)
        self._entries = out
        return out

    def extract(self, path_or_entry):
        if isinstance(path_or_entry, tuple):
            blk = path_or_entry[1]
        else:
            m = dict(self.entries())
            if path_or_entry not in m:
                raise KeyError(path_or_entry)
            blk = m[path_or_entry]
        return self._decode_whsc(self._vfile(blk))

    def extract_all(self, outdir, only=None):
        n = 0
        for p, blk in self.entries():
            if only and not only(p):
                continue
            dst = os.path.join(outdir, p.replace("\\", "/"))
            os.makedirs(os.path.dirname(dst) or outdir, exist_ok=True)
            try:
                data = self._decode_whsc(self._vfile(blk))
            except Exception as e:
                print("!! %s: %s" % (p, e))
                continue
            with open(dst, "wb") as f:
                f.write(data)
            n += 1
        return n

    def close(self):
        self.f.close()


# ---------- 写入 ----------
def _encrypt(plain, key8):
    """DES-CBC（无 IV）加密，尾部不足 8 字节走交换 + 链式异或，是 _decrypt 的逆过程。"""
    full = len(plain) // 8 * 8
    out = bytearray()
    prev_cipher = b"\0" * 8
    des = DES.new(key8, DES.MODE_ECB)
    for o in range(0, full, 8):
        x = bytes(plain[o + i] ^ prev_cipher[i] for i in range(8))
        c = des.encrypt(x)
        out += c
        prev_cipher = c
    r = len(plain) - full
    if r:
        t = bytearray(r)
        for i in range(r):
            prior = t[i - 1] if i else 0
            t[i] = plain[full + i] ^ key8[i] ^ prior ^ prev_cipher[i]
        parity = (t[-1] + t[0]) & 1
        for left in range(r // 2):
            right = r - 1 - left
            if ((t[right] + t[left]) & 1) == parity:
                t[left], t[right] = t[right], t[left]
        out += bytes(t)
    return bytes(out)


def encode_whsc(plain, flags=3, key=None):
    """明文 -> 完整 whsc 虚拟文件字节流（48 字节头 + 负载）。"""
    key = key or PKG_PWD
    comp = zlib.compress(plain, 9)
    body = _encrypt(comp, key[:8]) if flags & 2 else comp
    if flags & 1:
        stored = body
    else:
        stored = plain
    head = bytearray(48)
    head[0:7] = b"whsc1.0"
    head[15] = flags
    head[16:32] = hashlib.md5(plain).digest()
    struct.pack_into("<I", head, 36, len(plain))
    struct.pack_into("<I", head, 40, len(stored))
    struct.pack_into("<I", head, 44, 0x543B3338)
    return bytes(head) + stored


def blocks_for(size):
    """虚拟文件所需块数。"""
    return 1 if size <= BLOCK - 32 else 1 + -(-(size - (BLOCK - 32)) // (BLOCK - 8))


def patch_entry(path, entry_name, new_plain, backup_dir=None):
    """就地替换 dpk 中某个文件的内容（块数必须不变，否则拒绝）。

    返回 (旧明文长度, 新明文长度, 占用块数)。
    """
    d = Dpk(path)
    m = dict(d.entries())
    if entry_name not in m:
        d.close()
        raise KeyError("dpk 中没有 " + entry_name)
    root_blk = m[entry_name]
    root = d._read_at(root_blk * BLOCK, BLOCK)
    prev, nxt, tail, fsz = struct.unpack_from("<4I", root, 0)
    mtime = struct.unpack_from("<I", root, 16)[0]
    old_plain = d._decode_whsc(d._vfile(root_blk))

    # 收集块链
    chain, cur = [root_blk], nxt
    while cur:
        chain.append(cur)
        cur = struct.unpack_from("<I", d._read_at(cur * BLOCK, 8), 4)[0]
    d.close()

    new_vf = encode_whsc(new_plain, flags=3)
    if blocks_for(len(new_vf)) != len(chain):
        raise ValueError("新内容需要 %d 块，原有 %d 块，拒绝就地写入"
                         % (blocks_for(len(new_vf)), len(chain)))
    if len(new_vf) > (BLOCK - 32) + (len(chain) - 1) * (BLOCK - 8):
        raise ValueError("新内容超出块链容量")

    if backup_dir:
        os.makedirs(backup_dir, exist_ok=True)
        import time
        bak = os.path.join(backup_dir, "%s-%s.bin" % (
            os.path.basename(path), time.strftime("%Y%m%d-%H%M%S")))
        with open(bak, "wb") as f:
            with open(path, "rb") as src:
                f.write(src.read())
        print("已备份原包 ->", bak)

    first = new_vf[:BLOCK - 32]
    with open(path, "r+b") as f:
        # 根块
        head = struct.pack("<4I", 0, chain[1] if len(chain) > 1 else 0,
                           chain[-1], len(new_vf)) + struct.pack("<I", mtime) + b"\0" * 12
        f.seek(root_blk * BLOCK)
        f.write(head + first + b"\0" * (BLOCK - 32 - len(first)))
        # 后续块
        off = BLOCK - 32
        for i in range(1, len(chain)):
            chunk = new_vf[off:off + BLOCK - 8]
            off += BLOCK - 8
            h = struct.pack("<2I", chain[i - 1], chain[i + 1] if i + 1 < len(chain) else 0)
            f.seek(chain[i] * BLOCK)
            f.write(h + chunk + b"\0" * (BLOCK - 8 - len(chunk)))
        f.flush()
        os.fsync(f.fileno())

    # 回读校验
    d2 = Dpk(path)
    got = d2._decode_whsc(d2._vfile(root_blk))
    d2.close()
    if got != new_plain:
        raise ValueError("回读校验失败：内容不一致")
    return len(old_plain), len(new_plain), len(chain)


if __name__ == "__main__":
    import sys
    dpk = Dpk(sys.argv[1])
    es = dpk.entries()
    print("条目数:", len(es))
    for p, b in es[:40]:
        print("  ", p, "block", b)
