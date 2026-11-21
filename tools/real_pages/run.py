"""Usage: python3 run.py CAPTURE_ROOT OUTPUT_DIR PROBE_BINARY"""
import json
from pathlib import Path
import subprocess
import sys

root, out, binary = map(Path, sys.argv[1:])
manifest = json.loads(Path(__file__).with_name('pages.json').read_text())
ref = json.loads((out / 'reference.json').read_text())['results']
rows = []
for i, t in enumerate(manifest['pages'], 1):
    row = {'id': i}
    rows.append(row)
    for suffix in ('.png', '-after.png', '.log'):
        (out / f'lite-{i}{suffix}').unlink(missing_ok=True)
    directory = root / t['source']
    assert (directory / '.revision').read_text() == manifest['sources'][t['source']][1]
    if not ref[i-1]['passed'] or ref[i-1]['id'] != i:
        row['result'] = 'reference unvalidated'
        continue
    path = t['path'] + ('index.html' if t['path'].endswith('/') else '')
    mode = next(k for k in ('heading', 'manual', 'type', 'game') if k in t)
    cmd = [binary.resolve(), directory.resolve(), path, out / f'lite-{i}', mode,
           *[t.get(k, '') for k in ('click', 'type', 'read', 'expect')], t[mode]]
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=45)
        (out / f'lite-{i}.log').write_bytes(proc.stdout)
        lines = proc.stdout.decode(errors='replace').splitlines()
        marks = [line[6:] for line in lines if line.startswith('BENCH ')]
        row['result'] = marks[-1] if proc.returncode == 0 and marks else f'probe exited {proc.returncode}'
        if t.get('game') == 'hex' and row['result'].startswith('pass:'):
            # DOM state alone passed in an experimental build with an invisible board.
            # Decode the actual shell PNG; check ink inside the board and the button.
            sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'conformance'))
            from render_check import read_png
            png = read_png(out / f'lite-{i}-after.png')
            x,y,w,h = map(float, next(line[12:] for line in lines if line.startswith('BENCH-PAUSE ')).split())
            board = png.region(384, 284, 32, 32)
            button = png.region(int(x), int(y), int(w), int(h))
            ink = (44,62,80,255)
            row['pauseRect'] = [x,y,w,h]
            row['visibleInk'] = ink in board and ink in button and (255,255,255,255) in button
            if not row['visibleInk']:
                row['result'] = 'not-completed: board or pause ink missing from frame'
        if t.get('click') == '#decoded':
            row['edits'] = [line.removeprefix('BENCH-EDIT ') for line in lines if line.startswith('BENCH-EDIT ')]
            selected = [line.removeprefix('BENCH-SELECT ') for line in lines if line.startswith('BENCH-SELECT ')]
            row['selected'] = selected
            oracle = ref[i-1]
            if (not oracle.get('selected') or selected != [oracle['selected']] or
                    len(row['edits']) != len(t['type']) or row['edits'] != oracle.get('edits')):
                if row['result'].startswith('pass:'):
                    row['result'] = 'not-completed: selection or per-key trace differs from Chromium'
    except subprocess.TimeoutExpired:
        row['result'] = 'timeout: 45s budget'
    print(i, row['result'], flush=True)
(out / 'litebird.json').write_text(json.dumps(rows, indent=2))
