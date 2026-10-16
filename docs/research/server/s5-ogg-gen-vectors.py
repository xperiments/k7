#!/usr/bin/env python3
"""Regenerate s5-ogg-vectors.h from a libogg checkout.

    curl -sLO https://github.com/xiph/ogg/archive/refs/heads/master.tar.gz
    tar xzf master.tar.gz
    python3 s5-ogg-gen-vectors.py ogg-master/src/framing.c > s5-ogg-vectors.h

Reads the expected page headers (head1_0 .. head4_6) and the packet sequences
that libogg's own test_pack() calls use, and emits them as C arrays.
"""
import re, sys

src = open(sys.argv[1]).read()
cases = []
for m in re.finditer(r'const int packets\[\]=\{(.*?)\};\s*\n\s*const int \*headret\[\]=\{(.*?)\};',
                     src, re.S):
    vals = [int(x, 0) for x in re.findall(r'-?\d+', m.group(1))]
    heads = [h.strip() for h in m.group(2).split(',') if h.strip() and h.strip() != 'NULL']
    tail = src[m.end():m.end() + 400]
    lm = re.search(r'fprintf\(stderr,"(testing [^"]+)"', tail)
    label = lm.group(1).replace('testing ', '').rstrip('. ') if lm else '?'
    cases.append((label, vals, heads))

arrays = {}
for m in re.finditer(r'const int (head\d+_\w+)\[\]\s*=\s*\{(.*?)\};', src, re.S):
    arrays[m.group(1)] = [int(x, 0) for x in re.findall(r'0x[0-9a-fA-F]+|\d+', m.group(2))]

out = ['/* AUTO-GENERATED -- see s5-ogg-gen-vectors.py */',
       '#ifndef OGG_VECTORS_H', '#define OGG_VECTORS_H']
for name, vals in sorted(arrays.items()):
    out.append(f'static const int {name}[] = {{{",".join(str(v) for v in vals)}}};')
out.append('typedef struct { const char *label; int npk; const int *lens;'
           ' int nexp; const int *const *hdrs; } vec_case_t;')
for i, (label, vals, heads) in enumerate(cases):
    lens = [v for v in vals if v >= 0]
    out.append(f'static const int case{i}_lens[] = {{{",".join(str(v) for v in lens)}}};')
    hs = ',\n    '.join('(const int *)' + h for h in heads)
    out.append(f'static const int *case{i}_hdrs[] = {{\n    {hs}}};\n')
out.append('static const vec_case_t VEC_CASES[] = {')
for i, (label, vals, heads) in enumerate(cases):
    out.append(f'  {{"{label}", {len(vals)-1}, case{i}_lens, {len(heads)}, case{i}_hdrs}},')
out.append('};')
out.append('#define VEC_NCASES (sizeof(VEC_CASES)/sizeof(VEC_CASES[0]))')
out.append('#endif')
print('\n'.join(out))
