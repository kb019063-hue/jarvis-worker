#!/usr/bin/env python3
"""Compare the SVG text position lists against pinned headless Chromium.

`x`, `y`, `dx` and `dy` on `<text>` and `<tspan>` are SVGAnimatedLengthLists
(SVG 2 §4.6.3), not single lengths: one attribute holds a list, one entry per
glyph run. jsdom has neither `SVGAnimatedLengthList` nor `SVGLength` at all, so
it is no reference here; Chromium is.

Install the browser once as in the phase document, then run this script. The
reference and Litebird execute the same independent page script; mismatches
are printed individually, and any mismatch makes the exit status nonzero.

Usage:
    python3 tools/conformance/verify_svg_length_lists.py [--binary PATH]
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

HTML = '''<!doctype html><svg>
  <text id="t" x="10 20 30" y="5" dx="1 2" dy="3">hi</text>
  <text id="t2">plain</text>
  <tspan id="sp" x="7">b</tspan>
  <textPath id="tp" x="4">c</textPath>
  <text id="cm" x="1cm 2mm">a</text>
  <text id="bad" x="10 bogus 30">a</text>
  <text id="w" x="10,20 , 30">a</text>
</svg>'''

SCRIPT = r'''
var g = function (f) { try { return JSON.stringify(f()); } catch (e) { return 'THROWS ' + e.name; } };
var out = [];
function P(name, f) { out.push('PROBE ' + name + ': ' + g(f)); }
var t = document.getElementById('t'), t2 = document.getElementById('t2');
var sp = document.getElementById('sp'), tp = document.getElementById('tp');
var cm = document.getElementById('cm'), bad = document.getElementById('bad');
var w = document.getElementById('w');

P('animated brand', function () {
  return [t.x.constructor.name, t.x instanceof SVGAnimatedLengthList, typeof SVGAnimatedLengthList];
});
P('list brand', function () {
  return [t.x.baseVal.constructor.name, t.x.baseVal instanceof SVGLengthList, typeof SVGLengthList];
});
P('identity', function () { return [t.x === t.x, t.x.baseVal === t.x.baseVal, t.x.animVal === t.x.baseVal]; });
P('count', function () { return [t.x.baseVal.numberOfItems, t.x.baseVal.length, t.x.animVal.numberOfItems]; });
P('items', function () {
  var L = t.x.baseVal;
  return [L[0].value, L[1].value, L[2].value, L[3]];
});
P('items are SVGLength', function () {
  var L = t.x.baseVal;
  return [L[0].constructor.name, L[0] instanceof SVGLength, L.getItem(0) === L.getItem(0)];
});
P('no attribute', function () { return [t2.x.baseVal.numberOfItems, t.y.baseVal.numberOfItems]; });
P('four lists', function () {
  return [t.dx.constructor.name, t.dy.constructor.name, t.y.baseVal.numberOfItems];
});
P('tspan', function () { return [sp.x.baseVal.numberOfItems, sp.x.baseVal[0].value]; });
P('textPath has none', function () { return [typeof tp.x, typeof tp.dx]; });
P('units preserved', function () {
  var L = cm.x.baseVal;
  return [L[0].unitType, L[0].valueInSpecifiedUnits, L[0].valueAsString,
          L[1].unitType, L[1].valueInSpecifiedUnits, L[1].valueAsString];
});
P('comma and space', function () { return [w.x.baseVal.numberOfItems, w.x.baseVal[2].value]; });
P('invalid list is empty', function () { return bad.x.baseVal.numberOfItems; });
P('getItem out of range', function () { return t.x.baseVal.getItem(9); });
P('getItem negative', function () { return t.x.baseVal.getItem(-1); });
P('live write', function () {
  t.x.baseVal.getItem(1).value = 99;
  return [t.getAttribute('x'), t.x.baseVal.numberOfItems, t.x.baseVal[1].value];
});
P('write keeps unit', function () {
  cm.x.baseVal.getItem(0).value = 100;
  return cm.getAttribute('x');
});
P('removeItem', function () {
  var L = t.x.baseVal;
  L.removeItem(1);
  return [t.getAttribute('x'), L.numberOfItems];
});
P('insertItemBefore', function () {
  var L = t.x.baseVal;
  L.insertItemBefore(L.getItem(0), 0);
  return [t.getAttribute('x'), L.numberOfItems];
});
P('appendItem clamped index', function () {
  var L = t.x.baseVal;
  L.replaceItem(L.getItem(0), 9);
  return 'replaced';
});
P('clear removes attribute', function () {
  var n = document.createElementNS('http://www.w3.org/2000/svg', 'text');
  n.setAttribute('x', '1 2');
  var L = n.x.baseVal;
  L.clear();
  return [L.numberOfItems, n.getAttribute('x')];
});
P('indexed write needs an SVGLength', function () {
  var n = document.createElementNS('http://www.w3.org/2000/svg', 'text');
  n.setAttribute('x', '1 2');
  // Sloppy on purpose: the list's indexed setter type-checks its argument, so
  // a non-SVGLength throws whatever the strictness of the assignment.
  try { n.x.baseVal[0] = 5; } catch (e) { return [e.name, n.getAttribute('x')]; }
  return ['no error', n.getAttribute('x')];
});
P('indexed write of an SVGLength', function () {
  var n = document.createElementNS('http://www.w3.org/2000/svg', 'text');
  n.setAttribute('x', '1 2 3');
  n.x.baseVal[0] = n.x.baseVal.getItem(2);
  return [n.getAttribute('x'), n.x.baseVal.numberOfItems];
});
P('enumerates indices', function () {
  var n = document.createElementNS('http://www.w3.org/2000/svg', 'text');
  n.setAttribute('x', '1 2 3');
  var L = n.x.baseVal;
  return [Object.keys(L), 0 in L, 3 in L];
});
P('animVal items read-only', function () {
  var n = document.createElementNS('http://www.w3.org/2000/svg', 'text');
  n.setAttribute('x', '1 2');
  try { n.x.animVal.getItem(0).value = 5; } catch (e) { return [e.name, n.getAttribute('x')]; }
  return ['no error', n.getAttribute('x')];
});
P('single length still a length', function () {
  var s = document.querySelector('svg');
  var r = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
  r.setAttribute('x', '3');
  return [r.x.constructor.name, r.x.baseVal.value];
});
P('length value is user units', function () {
  var n = document.createElementNS('http://www.w3.org/2000/svg', 'rect');
  n.setAttribute('x', '1cm');
  n.x.baseVal.value = 100;
  return [n.getAttribute('x'), n.x.baseVal.unitType];
});
P('string tags', function () {
  return [Object.prototype.toString.call(t.x), Object.prototype.toString.call(t.x.baseVal)];
});
console.log(out.join('\n'));
'''

BROWSER = r'''
const puppeteer=require('puppeteer-core'), chromium=require('@sparticuz/chromium');
(async()=>{const b=await puppeteer.launch({executablePath:await chromium.executablePath(),args:chromium.args,headless:true});
try { const page=await b.newPage(); let lines=[];
// The answers arrive as one console line -- the script joins its output and
// logs it once -- and a page that writes a bad attribute also makes Chromium
// log a parse error of its own. Splitting on the marker rather than matching
// the line keeps only this script's answers: the first answer has no marker
// in front of it, so the text before the first marker is the log's own.
page.on('console',m=>{ const t=m.text(); if (!t.includes('PROBE ')) return;
  for (const part of t.split('PROBE ').slice(1)) lines.push(part.replace(/\n$/, '')); });
await page.setContent(process.env.SVG_HTML); await page.evaluate(process.env.SVG_SCRIPT); process.stdout.write(JSON.stringify(lines)); }
finally { await b.close(); }})().catch(e=>{console.error(e);process.exit(1)});
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    args = p.parse_args()
    env = dict(os.environ, NODE_PATH=str(ROOT / 'build/svg-oracle/node_modules'),
               LD_LIBRARY_PATH=str(ROOT / 'build/svg-oracle/runtime/lib') + ':' + os.environ.get('LD_LIBRARY_PATH', ''),
               SVG_HTML=HTML, SVG_SCRIPT=SCRIPT)
    browser = subprocess.run(['node', '-e', BROWSER], cwd=ROOT, env=env, capture_output=True, text=True,
                             timeout=60, check=True)
    expected = json.loads(browser.stdout)
    if not expected:
        raise ValueError('Chromium reference returned no answers')
    with tempfile.TemporaryDirectory(prefix='svg-length-lists-') as tmp:
        page, script = Path(tmp) / 'page.html', Path(tmp) / 'probe.js'
        page.write_text(HTML)
        script.write_text(SCRIPT)
        run = subprocess.run([str(ROOT / args.binary), '--run-script', str(script), str(page)],
                             cwd=ROOT, capture_output=True, text=True, timeout=60)
    if run.returncode:
        print(f'Litebird exited {run.returncode}: {run.stderr}')
        return 1
    # Litebird prints `console.log` verbatim; the reference answers arrived
    # with the `PROBE ` marker already stripped above.
    actual = [line[6:] if line.startswith('PROBE ') else line for line in run.stdout.splitlines()]
    print(f'Chromium: {len(expected)} answers; Litebird: {len(actual)} answers')
    good = 0
    for i in range(max(len(expected), len(actual))):
        a = actual[i] if i < len(actual) else '<missing>'
        e = expected[i] if i < len(expected) else '<missing>'
        if a == e:
            good += 1
            print('PASS', e)
        else:
            print('FAIL expected:', e, '\n     actual: ', a)
    print(f'SVG text position lists: {good}/{len(expected)} match Chromium')
    return int(good != len(expected) or len(actual) != len(expected))


if __name__ == '__main__':
    raise SystemExit(main())
