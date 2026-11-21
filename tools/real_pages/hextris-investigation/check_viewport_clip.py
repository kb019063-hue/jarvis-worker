"""Paint regression for the short-body Hextris failure. No downloaded game required."""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'conformance'))
from render_check import read_png

# Use normal flow to put the canvas outside a 10px body's padding box. A
# body clip must NOT disappear when html supplies overflow or containment.
for label, outer, body, wrapper, expected in [
    ('body overflow propagates', '', '', '', True),
    ('html already supplies overflow', 'overflow:hidden', '', '', False),
    ('body containment', '', 'contain:paint', '', False),
    ('inner box keeps its clip', '', '', 'height:12px;overflow:hidden', False),
]:
    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        page, png = root / 'case.html', root / 'frame.png'
        page.write_text(f'''<!doctype html><style>html{{{outer}}}body{{margin:0;height:10px;overflow:hidden;{body}}}
        section{{{wrapper}}}canvas{{display:block}}</style><section><canvas width="80" height="80"></canvas></section>
        <script>var c=document.querySelector('canvas').getContext('2d');c.fillStyle='rgb(44,62,80)';c.fillRect(0,0,80,80);</script>''')
        subprocess.run([Path(sys.argv[1]).resolve(), '--render-png', png, page], check=True, stdout=subprocess.DEVNULL)
        frame = read_png(png)
        for y, rgb in [(5,(44,62,80)), (40,(44,62,80) if expected else (255,255,255))]:
            if frame.at(5,y)[:3] != rgb:
                raise SystemExit(f'{label}: at (5,{y}) expected {rgb}, got {frame.at(5,y)}')
        print('PASS', label)
