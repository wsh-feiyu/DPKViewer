# -*- coding: utf-8 -*-
# 与 DesProbe3.cpp 对应的 Python 复算，逐轮打印同样的中间值
import re

src = open(r"C:/Users/wsh-0/.qianfan/workspace/sessions/e5a623892ec7401aaf3dc961906c0d48/2026-10-23/new-chat-3/DPKViewer/src/DPKViewer/Core/Des.cpp", "r", encoding="utf-8").read()

def get_array(name):
    m = re.search(r"(?:int|uint8_t)\s+" + name + r"\s*\[[^\]]*\](?:\s*\[[^\]]*\])?\s*=\s*\{(.*?)\};", src, re.S)
    return [int(x) for x in re.findall(r"-?\d+", m.group(1))]

IP = get_array("IP"); FP = get_array("FP"); E = get_array("E"); P = get_array("P")
PC1 = get_array("PC1"); PC2 = get_array("PC2"); SHIFTS = get_array("SHIFTS")
m = re.search(r"SBOX\[8\]\[64\]\s*=\s*\{(.*?)\};", src, re.S)
alln = [int(x) for x in re.findall(r"-?\d+", m.group(1))]
SBOX = [alln[i * 64:(i + 1) * 64] for i in range(8)]

def perm(inv, table, n, inbits):
    out = 0
    for i in range(n):
        out = (out << 1) | ((inv >> (inbits - table[i])) & 1)
    return out

def rotl28(v, c):
    return ((v << c) | (v >> (28 - c))) & 0x0FFFFFFF

def feistel(r, sub):
    x = perm(r, E, 48, 32) ^ sub
    s = 0
    for i in range(8):
        grp = (x >> (42 - i * 6)) & 0x3F
        row = (((grp >> 5) & 1) << 1) | (grp & 1)
        col = (grp >> 1) & 0x0F
        s = (s << 4) | SBOX[i][row * 16 + col]
    return perm(s, P, 32, 32)

key = 0x133457799BBCDFF1
k56 = perm(key, PC1, 56, 64)
c = (k56 >> 28) & 0x0FFFFFFF
d = k56 & 0x0FFFFFFF
print("k56 = %014X" % k56)
print("C0  = %07X  D0  = %07X" % (c, d))
subs = []
for i in range(16):
    c = rotl28(c, SHIFTS[i]); d = rotl28(d, SHIFTS[i])
    subs.append(perm((c << 28) | d, PC2, 48, 56))

blk = perm(0x0123456789ABCDEF, IP, 64, 64)
print("IP  = %016X" % blk)
l = (blk >> 32) & 0xFFFFFFFF
r = blk & 0xFFFFFFFF
print("L0  = %08X  R0  = %08X" % (l, r))
print("K1  = %012X  f(R0,K1) = %08X" % (subs[0], feistel(r, subs[0])))
l, r = r, l ^ feistel(r, subs[0])
print("L1  = %08X  R1  = %08X" % (l, r))
f2 = feistel(r, subs[1])
l, r = r, l ^ f2
print("L2  = %08X  R2  = %08X  f(R1,K2) = %08X" % (l, r, f2))
l, r = r, l ^ feistel(r, subs[2])
print("L3  = %08X  R3  = %08X" % (l, r))
print("E   = %016X" % perm((r << 32) | l, FP, 64, 64))