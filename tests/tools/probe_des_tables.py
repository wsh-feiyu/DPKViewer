# -*- coding: utf-8 -*-
# 从 Des.cpp 中提取表并按同一逻辑用 Python 复算，定位差异
import re, sys

src = open(r"C:/Users/wsh-0/.qianfan/workspace/sessions/e5a623892ec7401aaf3dc961906c0d48/2026-10-23/new-chat-3/DPKViewer/src/DPKViewer/Core/Des.cpp", "r", encoding="utf-8").read()

def get_array(name):
    m = re.search(r"(?:int|uint8_t)\s+" + name + r"\s*\[[^\]]*\](?:\s*\[[^\]]*\])?\s*=\s*\{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("not found: " + name)
    body = m.group(1)
    nums = [int(x) for x in re.findall(r"-?\d+", body)]
    return nums

IP = get_array("IP"); FP = get_array("FP"); E = get_array("E"); P = get_array("P")
PC1 = get_array("PC1"); PC2 = get_array("PC2"); SHIFTS = get_array("SHIFTS")
print("len:", len(IP), len(FP), len(E), len(P), len(PC1), len(PC2), len(SHIFTS))

SBOX = []
for i in range(8):
    m = re.search(r"const uint8_t SBOX\[8\]\[64\]\s*=\s*\{(.*?)\n\};", src, re.S)
m = re.search(r"SBOX\[8\]\[64\]\s*=\s*\{(.*?)\};", src, re.S)
body = m.group(1)
allnums = [int(x) for x in re.findall(r"-?\d+", body)]
assert len(allnums) == 512, len(allnums)
SBOX = [allnums[i*64:(i+1)*64] for i in range(8)]

def perm_from(inv, table, n, inbits):
    out = 0
    for i in range(n):
        out = (out << 1) | ((inv >> (inbits - table[i])) & 1)
    return out

def rotl28(v, c):
    return ((v << c) | (v >> (28 - c))) & 0x0FFFFFFF

key = 0x133457799BBCDFF1
k56 = perm_from(key, PC1, 56, 64)
c = (k56 >> 28) & 0x0FFFFFFF
d = k56 & 0x0FFFFFFF
subs = []
for i in range(16):
    c = rotl28(c, SHIFTS[i]); d = rotl28(d, SHIFTS[i])
    cd = (c << 28) | d
    subs.append(perm_from(cd, PC2, 48, 56))

for i, k in enumerate(subs):
    print("K%-2d = %012X" % (i + 1, k))

def feistel(r, sub):
    x = perm_from(r, E, 48, 32) ^ sub
    s = 0
    for i in range(8):
        grp = (x >> (42 - i * 6)) & 0x3F
        row = (((grp >> 5) & 1) << 1) | (grp & 1)
        col = (grp >> 1) & 0x0F
        s = (s << 4) | SBOX[i][row * 16 + col]
    return perm_from(s, P, 32, 32)

def crypt(block):
    blk = perm_from(block, IP, 64, 64)
    l = (blk >> 32) & 0xFFFFFFFF; r = blk & 0xFFFFFFFF
    for i in range(16):
        l, r = r, l ^ feistel(r, subs[i])
    pre = (r << 32) | l
    return perm_from(pre, FP, 64, 64)

ct = crypt(0x0123456789ABCDEF)
print("python-from-cpp-tables E = %016X" % ct)
try:
    from Crypto.Cipher import DES as PDC
    c2 = PDC.new(bytes.fromhex("133457799BBCDFF1"), PDC.MODE_ECB)
    print("pycryptodome        E = %s" % c2.encrypt(bytes.fromhex("0123456789ABCDEF")).hex().upper())
except Exception as ex:
    print("pycryptodome 不可用:", ex)