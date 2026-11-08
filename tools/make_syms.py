"""Builds reference/t2k.sym (name -> absolute address) from rmac's listing of yak.s.
Link layout (mwenge Makefile: rln -a 802000 4000 efa8, yak.cof first): text $802000, data $4000, bss $EFA8."""
import re, sys
lst = sys.argv[1] if len(sys.argv) > 1 else 'reference/tempest2k/symout/yak.lst'
out = sys.argv[2] if len(sys.argv) > 2 else 'reference/t2k.sym'
base = {'t': 0x802000, 'd': 0x4000, 'b': 0xEFA8}
text = open(lst, encoding='latin-1').read()
syms = {}
for part in text.split('Symbol Table')[1:]:
    for name, val, typ in re.findall(r'(\S+) ([0-9A-F]{16})\s+([a-z]+)', part):
        t = typ[0]
        if t in base:
            syms[name] = int(val, 16) + base[t]
with open(out, 'w') as f:
    for n, a in sorted(syms.items(), key=lambda kv: kv[1]):
        f.write(f'{a:06X} {n}\n')
print(len(syms), 'symbols ->', out)
