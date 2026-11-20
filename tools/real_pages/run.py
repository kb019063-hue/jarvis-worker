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
        marks = [line[6:] for line in proc.stdout.decode(errors='replace').splitlines() if line.startswith('BENCH ')]
        row['result'] = marks[-1] if proc.returncode == 0 and marks else f'probe exited {proc.returncode}'
    except subprocess.TimeoutExpired:
        row['result'] = 'timeout: 45s budget'
    print(i, row['result'], flush=True)
(out / 'litebird.json').write_text(json.dumps(rows, indent=2))
