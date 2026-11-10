#!/usr/bin/env python3
"""Resumo curto do dkr360.log: python profsum.py <log> [ultimas N janelas prof]."""
import re, sys
path = sys.argv[1]
n = int(sys.argv[2]) if len(sys.argv) > 2 else 6
L = open(path, encoding="latin1").read().split("\n")
rows = []
for l in L:
    m = re.match(r"prof: (\d+) frames in (\d+) ms \(worst ([\d.]+) ms, (\d+) over 20 ms\); ms/s: walk ([\d.]+)/\d+ replay ([\d.]+)/\d+ endframe ([\d.]+)/\d+ audio ([\d.]+)/\d+ pace ([\d.]+)/\d+.*?flush ([\d.]+)/\d+ bind ([\d.]+)/\d+ vtx ([\d.]+)/\d+ poly ([\d.]+)/\d+ scan ([\d.]+)/\d+.*\| game\+other ([\d.]+)", l)
    if m:
        rows.append([float(x) for x in m.groups()])
print("frames worst over | walk replay endf audio pace | flush bind vtx poly scan | game")
for r in rows[-n:]:
    print("%d %.0f %d | %.0f %.0f %.0f %.0f %.0f | %.0f %.0f %.0f %.0f %.0f | %.0f" % (r[0], r[2], r[3], r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11], r[12], r[13], r[14]))
for l in L:
    if l.startswith("lookups:") or l.startswith("lead:"):
        last = l
print(last if "last" in dir() else "")
