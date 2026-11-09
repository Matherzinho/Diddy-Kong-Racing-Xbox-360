#!/usr/bin/env python3
"""Aplicador mínimo de VCDIFF/xdelta (RFC 3284, sem compressão secundária).
uso: vcdiff_apply.py origem.z64 patch.xdelta saida.z64"""
import sys, zlib

def build_table():
    # tabela de códigos padrão do RFC 3284: lista de (tipo1,tam1,modo1,tipo2,tam2,modo2)
    NOOP, ADD, RUN, COPY = 0, 1, 2, 3
    t = []
    t.append((RUN, 0, 0, NOOP, 0, 0))
    for s in range(0, 18):
        t.append((ADD, s, 0, NOOP, 0, 0))
    for mode in range(9):
        t.append((COPY, 0, mode, NOOP, 0, 0))
        for s in range(4, 19):
            t.append((COPY, s, mode, NOOP, 0, 0))
    for m in range(6):
        for a in range(1, 5):
            for c in range(4, 7):
                t.append((ADD, a, 0, COPY, c, m))
    for m in range(6, 9):
        for a in range(1, 5):
            t.append((ADD, a, 0, COPY, 4, m))
    for m in range(9):
        t.append((COPY, 4, m, ADD, 1, 0))
    assert len(t) == 256, len(t)
    return t

TABLE = build_table()

def varint(d, p):
    v = 0
    while True:
        b = d[p]; p += 1
        v = (v << 7) | (b & 0x7f)
        if not b & 0x80:
            return v, p

def apply(src, d):
    assert d[:3] == b'\xd6\xc3\xc4', 'não é VCDIFF'
    p = 4
    hi = d[p]; p += 1
    assert not hi & 1, 'compressão secundária não suportada'
    assert not hi & 2, 'tabela de códigos custom não suportada'
    if hi & 4:
        n, p = varint(d, p); p += n
    out = bytearray()
    while p < len(d):
        wi = d[p]; p += 1
        if wi & 3:
            ln, p = varint(d, p)
            pos, p = varint(d, p)
            seg = src if wi & 1 else bytes(out)
            sdata = seg[pos:pos + ln]
        else:
            sdata = b''
        dl, p = varint(d, p)
        end = p + dl
        tl, p = varint(d, p)
        di = d[p]; p += 1
        assert di == 0, 'compressão secundária na janela'
        adl, p = varint(d, p)
        il, p = varint(d, p)
        cl, p = varint(d, p)
        adler = None
        if wi & 4:
            adler = int.from_bytes(d[p:p + 4], 'big'); p += 4
        adds = d[p:p + adl]; p += adl
        insts = d[p:p + il]; p += il
        addrs = d[p:p + cl]; p += cl
        assert p == end
        tgt = bytearray()
        near = [0] * 4; same = [0] * 768; nxt = 0
        ap = 0; dp = 0; ip = 0
        def get_addr(mode, here):
            nonlocal ap, nxt
            if mode == 0:
                a, ap = varint(addrs, ap)
            elif mode == 1:
                v, ap = varint(addrs, ap); a = here - v
            elif mode - 2 < 4:
                v, ap = varint(addrs, ap); a = near[mode - 2] + v
            else:
                b = addrs[ap]; ap += 1
                a = same[(mode - 6) * 256 + b]
            near[nxt] = a; nxt = (nxt + 1) % 4
            same[a % 768] = a
            return a
        full = sdata  # espaço de endereços: fonte + alvo
        while ip < len(insts):
            idx = insts[ip]; ip += 1
            for k in (0, 3):
                typ, size, mode = TABLE[idx][k:k + 3]
                if typ == 0:
                    continue
                if size == 0 and typ != 0:
                    size, ip = varint(insts, ip)
                if typ == 1:
                    tgt += adds[dp:dp + size]; dp += size
                elif typ == 2:
                    tgt += bytes([adds[dp]]) * size; dp += 1
                else:
                    here = len(sdata) + len(tgt)
                    a = get_addr(mode, here)
                    for i in range(size):
                        q = a + i
                        tgt.append(sdata[q] if q < len(sdata) else tgt[q - len(sdata)])
        assert len(tgt) == tl, (len(tgt), tl)
        if adler is not None:
            assert zlib.adler32(bytes(tgt)) == adler, 'adler32 diferente'
        out += tgt
    return bytes(out)

if __name__ == '__main__':
    s = open(sys.argv[1], 'rb').read()
    r = apply(s, open(sys.argv[2], 'rb').read())
    open(sys.argv[3], 'xb').write(r)
    print(len(r), 'bytes ok')
