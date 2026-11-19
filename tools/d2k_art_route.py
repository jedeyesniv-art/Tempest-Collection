import sys
mode = sys.argv[1]
a = ['--press=300:b']
if mode == 'classic': a += ['--press=450:down', '--press=480:b']
if mode == 'plus': a += ['--press=450:down', '--press=560:down', '--press=590:b']
a += [f'--press={f}:b' for f in (700, 1000, 1300, 1600)]
for f in range(1800, 16000, 12): a.append(f'--press={f}:b:3')
for i, f in enumerate(range(1810, 16000, 200)): a.append(f'--press={f}:{"up" if i % 2 else "down"}:40')
for f in range(1900, 16000, 400): a.append(f'--press={f}:thrust:120')
for f in range(2300, 16000, 800): a.append(f'--press={f}:reverse:2')
for f in range(2500, 16000, 1500): a.append(f'--press={f}:y:3')
if mode == 'plus':
    for f in range(2000, 16000, 300): a.append(f'--press={f}:a:3')
print(' '.join(a))
