"""Strict snapshot gate, independent of interaction score. Requires Pillow.
Usage: python3 strict_frames.py RUN_DIRECTORY > strict-frames.json
"""
import json
import sys
from pathlib import Path
from PIL import Image, ImageChops

run = Path(sys.argv[1])
manifest = json.loads(Path(__file__).with_name('pages.json').read_text())['pages']
ref = json.loads((run / 'reference.json').read_text())['results']
lite = json.loads((run / 'litebird.json').read_text())
if len(manifest) != len(lite) or len(ref) != len(lite):
    sys.exit('incomplete run')
rows = []
for i, (page, oracle, observed) in enumerate(zip(manifest, ref, lite), 1):
    if i != observed['id'] or i != oracle['id']:
        sys.exit('out-of-order run')
    row = {'id': i}
    rows.append(row)
    if not oracle['passed'] or not observed['result'].startswith('pass:'):
        row['verdict'] = 'not comparable: no verified task completion'
    elif page.get('game') == '2048':
        row['verdict'] = 'not comparable: independent random boards'
    else:
        # A missing post-action PNG is an error, not permission to compare initial frames.
        with Image.open(run / f'ref-{i}-after.png') as a, Image.open(run / f'lite-{i}-after.png') as b:
            if a.size != (800, 600) or b.size != a.size:
                row['verdict'] = 'fail: wrong frame dimensions'
                continue
            bands = ImageChops.difference(a.convert('RGBA'), b.convert('RGBA')).split()
            mask = bands[0]
            for band in bands[1:]:
                mask = ImageChops.lighter(mask, band)
            changed = 800 * 600 - mask.histogram()[0]
            row.update(verdict='exact' if changed == 0 else 'fail', different_pixels=changed, pixels=480000)
print(json.dumps({'rule': 'Post-task RGBA; same viewport; zero different pixels allowed. No alignment, masking or tolerance.', 'results': rows}, indent=2))
