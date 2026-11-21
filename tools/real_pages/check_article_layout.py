"""Small pixel regression for article layout. Usage: python3 check_article_layout.py build/litebird

No capture download required. Sample solid interiors rather than font-dependent edges.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

html = '''<!doctype html><style>
body {margin:0} #table {display:table; width:160px; border-spacing:0}
.strip {height:12px; background:#aa3322} .strip:has(> em) {background:#229944}
.limit {max-width:100px} .wide {width:600px;height:8px}
.clip {overflow:hidden;width:50px;height:18px} .tail {background:#3344bb;height:12px}
</style><div id="table"><div class="strip"></div><div class="strip"><em></em></div>
<div class="limit"><div class="wide"></div></div></div><div class="clip">clipped text</div>
<div class="tail"></div>'''
with tempfile.TemporaryDirectory() as tmp:
    page, frame = (Path(tmp) / x for x in ('test.html', 'test.bmp'))
    page.write_text(html)
    subprocess.run([Path(sys.argv[1]).resolve(), '--render-bmp', frame, page], check=True,
                   stdout=subprocess.DEVNULL, timeout=15)
    bmp = frame.read_bytes()
    offset, = struct.unpack_from('<I', bmp, 10)
    w, h = struct.unpack_from('<ii', bmp, 18)
    depth, = struct.unpack_from('<H', bmp, 28)
    assert w > 180 and h > 100 and depth == 24

    def pixel(x, y):
        i = offset + (h-1-y)*((w*3+3)//4*4) + x*3
        return bmp[i:i+3][::-1].hex()

    for x, y, expected, description in [
        (10, 5, 'aa3322', 'non-matching :has does not color the first child'),
        (10, 18, '229944', 'second child follows first and matches :has'),
        (175, 5, 'ffffff', 'bounded content does not widen the table'),
        (100, 55, '3344bb', 'sibling still paints after clipped inline text'),
    ]:
        got = pixel(x, y)
        if got != expected:
            sys.exit(f'{description}: pixel ({x}, {y}) was {got}, expected {expected}')
    print('4/4 article-layout pixel checks passed')
