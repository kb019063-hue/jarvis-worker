#!/usr/bin/env python3
"""Differential: getBBox on use references to SVG viewports, against Chromium.

Uses the pinned browser installation documented in Phase 433. Every case has
its own attached reference and instance so unresolved or detached elements are
not mistaken for valid zero geometry.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
from verify_svg_bbox import BROWSER, ROOT

SHAPES = {
    'rect': '<rect x="1" y="2" width="3" height="4"/>',
    'percent': '<rect x="10%" y="20%" width="30%" height="40%"/>',
    'degenerate': '<rect x="1" y="2" width="0" height="4"/>',
    'group': '<g transform="translate(1,2)"><rect width="3" height="4"/></g>',
    'nested-percent': '<svg x="10%" y="20%" width="50%" height="50%"><rect x="10%" y="20%" width="30%" height="40%"/></svg>',
    'nested-zero': '<svg x="2" y="3" width="0" height="10" viewBox="0 0 5 5"><rect x="1" y="1" width="2" height="2"/></svg>',
    'nested-zero-pct': '<svg x="2" y="3" width="0" height="10"><rect x="10%" y="20%" width="30%" height="40%"/></svg>',
    'nested-none': '<svg x="2" y="3" width="10" height="5" viewBox="0 0 5 5" preserveAspectRatio="none"><rect x="1" y="1" width="2" height="2"/></svg>',
    'nested': '<svg x="2" y="3" width="10" height="10" viewBox="0 0 5 5"><rect x="1" y="1" width="2" height="2"/></svg>',
    'union': '<rect x="1" y="2" width="0" height="4"/><rect x="10" y="10" width="4" height="5"/>',
}
TARGETS = {
    'viewbox': 'x="2" y="3" width="20" height="10" viewBox="0 0 10 10"',
    'none': 'x="2" y="3" width="20" height="10" viewBox="0 0 10 10" preserveAspectRatio="none"',
    'slice': 'x="2" y="3" width="20" height="10" viewBox="0 0 10 10" preserveAspectRatio="xMaxYMax slice"',
    'offset': 'x="2" y="3" width="20" height="10" viewBox="10 20 10 10"',
    'plain': 'x="2" y="3" width="20" height="10"',
    'percent': 'x="10%" y="20%" width="50%" height="50%"',
    'default': 'viewBox="0 0 10 10"',
    'transformed': 'x="2" y="3" width="20" height="10" viewBox="0 0 10 10" transform="translate(11,13)"',
}
USES = {
    'default': '',
    'wide': 'width="20" height="10"',
    'width-only': 'width="10"',
    'height-only': 'height="5"',
    'zero-percent': 'width="0%" height="100%"',
    'negative-percent': 'width="-10%" height="10"',
    'negative-height': 'width="20" height="-1"',
    'square': 'width="10" height="10"',
    'percent': 'width="50%" height="50%"',
    'zero-width': 'width="0" height="10"',
    'zero-height': 'width="20" height="0"',
    'negative-width': 'width="-1" height="10"',
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    args = parser.parse_args()
    sources, uses, ids = [], [], []
    for shape, content in SHAPES.items():
        for target, attrs in TARGETS.items():
            source = f'src--{shape}--{target}'
            sources.append(f'<svg id="{source}" {attrs}>{content}</svg>')
            for kind, use_attrs in USES.items():
                ident = f'use--{shape}--{target}--{kind}'
                uses.append(f'<use id="{ident}" href="#{source}" x="5" y="7" {use_attrs}/>')
                ids.append(ident)
    html = '<!doctype html><svg width="200" height="100" viewBox="0 0 20 10"><defs>' + ''.join(sources) + '</defs>' + ''.join(uses) + '</svg>'
    extra = ['mut-width', 'mut-viewbox', 'mut-source-x', 'mut-use-x', 'mut-child-width', 'mut-zero-width']
    script = '''var ids = %s; var out = [];
for (var i=0; i<ids.length; ++i) {
  try { var b=document.getElementById(ids[i]).getBBox();
    out.push('PROBE '+ids[i]+': '+JSON.stringify([b.x,b.y,b.width,b.height].map(function(v){return Math.round(v*1000)/1000;})));
  } catch(e) {out.push('PROBE '+ids[i]+': THROWS '+e.name);}
}
function changed(name, n) { var b=n.getBBox();
  out.push('PROBE '+name+': '+JSON.stringify([b.x,b.y,b.width,b.height].map(function(v){return Math.round(v*1000)/1000;})));
}
var u=document.getElementById('use--rect--viewbox--wide');
var source=document.getElementById('src--rect--viewbox');
u.setAttribute('width','10'); changed('mut-width',u);
source.setAttribute('viewBox','0 0 20 10'); changed('mut-viewbox',u);
source.setAttribute('x','4'); changed('mut-source-x',u);
u.setAttribute('x','10'); changed('mut-use-x',u);
source.firstElementChild.setAttribute('width','6'); changed('mut-child-width',u);
u.setAttribute('width','0'); changed('mut-zero-width',u);
console.log(out.join('\\n'));''' % json.dumps(ids)
    env = dict(os.environ, NODE_PATH=str(ROOT/'build/svg-oracle/node_modules'),
               LD_LIBRARY_PATH=str(ROOT/'build/svg-oracle/runtime/lib') + ':' + os.environ.get('LD_LIBRARY_PATH', ''),
               SVG_HTML=html, SVG_SCRIPT=script)
    browser = subprocess.run(['node','-e',BROWSER], cwd=ROOT, env=env, capture_output=True, text=True, timeout=60, check=True)
    expected = json.loads(browser.stdout)
    with tempfile.TemporaryDirectory(prefix='svg-use-viewport-') as tmp:
        page, js = Path(tmp)/'page.html', Path(tmp)/'probe.js'
        page.write_text(html); js.write_text(script)
        run = subprocess.run([str(ROOT / args.binary),'--run-script',str(js),str(page)], cwd=ROOT, capture_output=True, text=True, timeout=120, check=True)
    actual = [line[6:] if line.startswith('PROBE ') else line for line in run.stdout.splitlines()]
    n = len(ids) + len(extra)
    if len(expected) != n or len(actual) != n:
        print('Invalid result counts:', len(expected), len(actual), 'expected', n); return 1
    errors = 0
    for e, a in zip(expected, actual):
        if e != a:
            errors += 1
            print('FAIL expected:', e, '\n     actual: ', a)
    print(f'SVG use viewport: {n-errors}/{n} match Chromium')
    return int(errors != 0)

if __name__ == '__main__':
    raise SystemExit(main())
