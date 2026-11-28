#!/usr/bin/env python3
"""Compare painting of a <use>-referenced SVG viewport against Chromium.

This is an open defect found in Phase 525. Install the pinned browser as in
Phase 433, then run this script. A mismatch returns nonzero; it is not a
known-failure exception in the render conformance scoreboard.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
from verify_svg_bbox import ROOT
from render_check import read_png

HTML = '''<!doctype html><style>body{margin:0}</style>
<svg width="100" height="50" viewBox="0 0 100 50"><defs>
<svg id="src" width="20" height="10" viewBox="0 0 10 10" preserveAspectRatio="none">
<rect width="10" height="10" fill="#00ff00"/></svg></defs>
<use href="#src" width="40" height="20"/></svg>'''
BROWSER = r'''
const p=require('puppeteer-core'),c=require('@sparticuz/chromium');
(async()=>{let b=await p.launch({executablePath:await c.executablePath(),args:c.args,headless:true});
try {let page=await b.newPage();await page.setViewport({width:800,height:600});
await page.setContent(process.env.SVG_HTML);await page.screenshot({path:process.env.SVG_PNG,omitBackground:true});}
finally {await b.close();}})().catch(e=>{console.error(e);process.exit(1)});
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', default='build/litebird')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='svg-use-paint-') as tmp:
        page, expected_png, actual_png = (Path(tmp)/p for p in ('page.html', 'browser.png', 'litebird.png'))
        page.write_text(HTML)
        env = dict(os.environ, NODE_PATH=str(ROOT/'build/svg-oracle/node_modules'),
                   LD_LIBRARY_PATH=str(ROOT/'build/svg-oracle/runtime/lib') + ':' + os.environ.get('LD_LIBRARY_PATH', ''),
                   SVG_HTML=HTML, SVG_PNG=str(expected_png))
        subprocess.run(['node', '-e', BROWSER], cwd=ROOT, env=env, check=True, timeout=60)
        subprocess.run([str(ROOT/args.binary), '--render-png', str(actual_png), str(page)], cwd=ROOT,
                       check=True, timeout=120, capture_output=True, text=True)
        expected, actual = read_png(expected_png), read_png(actual_png)
        points = [(5, 5), (15, 5), (25, 5), (35, 5), (5, 15), (25, 15)]
        failures = 0
        for x, y in points:
            e, a = expected.at(x, y), actual.at(x, y)
            if e != a:
                failures += 1
                print(f'FAIL ({x},{y}): Chromium {e}, Litebird {a}')
        print(f'SVG use viewport paint: {len(points)-failures}/{len(points)} match Chromium')
        return int(failures != 0)

if __name__ == '__main__':
    raise SystemExit(main())
