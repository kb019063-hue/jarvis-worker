#!/usr/bin/env python3
"""Differential for degenerate SVG symbol instance geometry against Chromium.

Requires the pinned Chromium installation described in Phase 433. Cases are
attached to the document; each independent instance has a unique source.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
from verify_svg_bbox import BROWSER, ROOT

SHAPES = {
    'percent': '<rect x="10%" y="20%" width="30%" height="40%"/>',
    'rect-zero-width': '<rect x="1" y="2" width="0" height="4"/>',
    'rect-zero-height': '<rect x="1" y="2" width="3" height="0"/>',
    'line-origin': '<line x1="0" y1="0" x2="0" y2="0"/>',
    'path-origin': '<path d="M 0 0 L 0 0"/>',
    'line-point': '<line x1="1" y1="2" x2="1" y2="2"/>',
    'path-point': '<path d="M 1 2 L 1 2"/>',
    'absolute': '<rect x="1" y="2" width="3" height="4"/>',
    'mixed-width': '<rect x="1" y="2" width="30%" height="4"/>',
    'mixed-height': '<rect x="1" y="2" width="3" height="40%"/>',
    'mixed-position': '<rect x="10%" y="20%" width="3" height="4"/>',
    'line': '<line x1="10%" y1="20%" x2="70%" y2="80%"/>',
    'circle': '<circle cx="10%" cy="20%" r="20%"/>',
    'nested-use-zero': '<use href="#zero-ref"/><rect x="10" y="10" width="4" height="5"/>',
    'point-and-rect': '<line x1="0" y1="0" x2="0" y2="0"/><rect x="10" y="10" width="4" height="5"/>',
    'two-children': '<rect x="10%" y="20%" width="30%" height="40%"/><rect x="2" y="3" width="4" height="5"/>',
    'group': '<g transform="translate(3,4)"><rect x="10%" y="20%" width="30%" height="40%"/></g>',
}
SIZES = [('width-zero', '0', '10'), ('height-zero', '20', '0'),
         ('both-zero', '0', '0'), ('width-negative', '-1', '10'),
         ('height-negative', '20', '-1'), ('width-zero-percent', '0%', '10'),
         ('height-zero-percent', '20', '0%'), ('width-negative-percent', '-10%', '10'),
         ('height-negative-percent', '20', '-10%'), ('positive-percent', '50%', '50%'),
         ('normal', '20', '10')]


def main():
    symbols = []
    uses = []
    for name, shape in SHAPES.items():
        for variant, vb in [('novb', ''), ('viewbox', ' viewBox="0 0 10 10"')]:
            ident = name + '-' + variant
            symbols.append(f'<symbol id="{ident}"{vb}>{shape}</symbol>')
            for size, w, h in SIZES:
                uses.append(f'<use id="{ident}-{size}" href="#{ident}" x="5" y="7" width="{w}" height="{h}"/>')
    html = '<!doctype html><svg width="200" height="100" viewBox="0 0 20 10"><defs>' + '<rect id="zero-ref" x="1" y="2" width="0" height="4"/>' + ''.join(symbols) + '</defs>' + ''.join(uses) + '</svg>'
    ids = [f'{shape}-{v}-{size}' for shape in SHAPES for v in ['novb', 'viewbox'] for size, _, _ in SIZES]
    script = '''var ids = %s; var out = [];
for (var i=0; i<ids.length; ++i) {
  try { var b=document.getElementById(ids[i]).getBBox();
    out.push('PROBE '+ids[i]+': '+JSON.stringify([b.x,b.y,b.width,b.height].map(function(v){return Math.round(v*1000)/1000;})));
  } catch(e) {out.push('PROBE '+ids[i]+': THROWS '+e.name);}
}
console.log(out.join('\\n'));''' % json.dumps(ids)
    env = dict(os.environ, NODE_PATH=str(ROOT / 'build/svg-oracle/node_modules'),
               LD_LIBRARY_PATH=str(ROOT / 'build/svg-oracle/runtime/lib') + ':' + os.environ.get('LD_LIBRARY_PATH', ''),
               SVG_HTML=html, SVG_SCRIPT=script)
    browser = subprocess.run(['node', '-e', BROWSER], cwd=ROOT, env=env, capture_output=True, text=True, timeout=60, check=True)
    expected = json.loads(browser.stdout)
    with tempfile.TemporaryDirectory(prefix='svg-symbol-') as tmp:
        page, js = Path(tmp) / 'page.html', Path(tmp) / 'probe.js'
        page.write_text(html); js.write_text(script)
        run = subprocess.run([str(ROOT/'build/litebird'), '--run-script', str(js), str(page)], cwd=ROOT, capture_output=True, text=True, timeout=120, check=True)
    actual = [line[6:] if line.startswith('PROBE ') else line for line in run.stdout.splitlines()]
    if len(expected) != len(ids) or len(actual) != len(ids):
        print('Invalid result counts:', len(expected), len(actual), 'expected', len(ids))
        return 1
    errors = 0
    for e, a in zip(expected, actual):
        if e != a:
            errors += 1
            print('FAIL expected:', e, '\n     actual: ', a)
    print(f'SVG symbol degenerate: {len(ids)-errors}/{len(ids)} match Chromium')
    return int(errors != 0)

if __name__ == '__main__':
    raise SystemExit(main())
