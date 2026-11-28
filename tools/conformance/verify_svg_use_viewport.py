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
            source = f'{shape}-{target}'
            sources.append(f'<svg id="{source}" {attrs}>{content}</svg>')
            for kind, use_attrs in USES.items():
                ident = f'{source}-{kind}'
                uses.append(f'<use id="{ident}" href="#{source}" x="5" y="7" {use_attrs}/>')
                ids.append(ident)
    html = '<!doctype html><svg width="200" height="100" viewBox="0 0 20 10"><defs>' + ''.join(sources) + '</defs>' + ''.join(uses) + '</svg>'
    script = '''var ids = %s; var out = [];
for (var i=0; i<ids.length; ++i) {
  try { var b=document.getElementById(ids[i]).getBBox();
    out.push('PROBE '+ids[i]+': '+JSON.stringify([b.x,b.y,b.width,b.height].map(function(v){return Math.round(v*1000)/1000;})));
  } catch(e) {out.push('PROBE '+ids[i]+': THROWS '+e.name);}
}
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
    if len(expected) != len(ids) or len(actual) != len(ids):
        print('Invalid result counts:', len(expected), len(actual), 'expected', len(ids)); return 1
    errors = 0
    for e, a in zip(expected, actual):
        if e != a:
            errors += 1
            print('FAIL expected:', e, '\n     actual: ', a)
    print(f'SVG use viewport: {len(ids)-errors}/{len(ids)} match Chromium')
    return int(errors != 0)

if __name__ == '__main__':
    raise SystemExit(main())
