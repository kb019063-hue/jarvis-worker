#!/usr/bin/env python3
"""Compare SVG getBBox() against pinned headless Chromium.

`SVGGraphicsElement.getBBox()` (SVG 2 §8.10) is the box of the geometry an
element renders, in its own user space: the element's own `transform` is
excluded, its children's are included, and stroke is not part of it. An SVG
element has no layout box at all, so a value taken from one is always empty --
which is exactly what this engine shipped, and what this script exists to keep
from coming back.

jsdom has no `getBBox()` (it answers `undefined` on an `SVGElement` and has no
`SVGGraphicsElement` to hang it on), so the reference is Chromium.

Install the browser once as in the phase document, then run this script. The
reference and Litebird execute the same independent page script; mismatches are
printed individually, and any mismatch makes the exit status nonzero.

Usage:
    python3 tools/conformance/verify_svg_bbox.py [--binary PATH]
"""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]

HTML = '''<!doctype html><svg id="s" width="200" height="100" viewBox="0 0 20 10">
  <defs><rect id="source" x="1" y="2" width="3" height="4" transform="scale(2)"/><g id="source-group"><rect x="2" y="3" width="4" height="5"/></g><use id="cycle" href="#cycle"/></defs>
  <use id="instance" href="#source" x="10" y="20"/>
  <use id="group-instance" href="#source-group" x="4" y="5"/>
  <use id="bad-instance" href="#cycle"/>
  <g id="g">
    <rect id="r" x="1" y="2" width="3" height="4"/>
    <circle id="c" cx="10" cy="10" r="4"/>
    <line id="l" x1="0" y1="0" x2="6" y2="8"/>
    <ellipse id="e" cx="5" cy="5" rx="3" ry="2"/>
    <polygon id="pg" points="0,0 10,0 5,10"/>
    <polyline id="pl" points="0,0 4,4 8,0"/>
    <path id="pa" d="M0 0 L10 10 L20 0"/>
  </g>
  <rect id="round" x="2" y="2" width="8" height="6" rx="2"/>
</svg>'''

SCRIPT = r'''
var g = function (f) { try { return JSON.stringify(f()); } catch (e) { return 'THROWS ' + e.name; } };
var out = [];
function P(name, f) { out.push('PROBE ' + name + ': ' + g(f)); }
// Chromium answers 0,0,0,0 for an element that is not in the document at all
// (a detached element has no layout), so every element a probe makes is put in
// the document, measured, and taken out again. Without this the reference
// would be all zeroes and this test would grade the wrong thing.
var SVG_NS = 'http://www.w3.org/2000/svg';
function attached(node, f) {
  var svg = document.getElementById('s');
  svg.appendChild(node);
  try { return f(node); } finally { svg.removeChild(node); }
}
function boxOf(node) {
  var b = node.getBBox();
  return [b.x, b.y, b.width, b.height].map(function (v) { return Math.round(v * 1000) / 1000; });
}
function box(id) {
  var b = document.getElementById(id).getBBox();
  // Three decimals: a browser's own arithmetic is float, and the last digits of
  // a curve bound are not what this test is about.
  return [b.x, b.y, b.width, b.height].map(function (v) { return Math.round(v * 1000) / 1000; });
}
var ids = ['instance', 'group-instance', 'bad-instance', 'g', 'r', 'c', 'l', 'e', 'pg', 'pl', 'pa', 'round'];
for (var i = 0; i < ids.length; ++i) {
  (function (id) { P('shape ' + id, function () { return box(id); }); })(ids[i]);
}

P('is an SVGRect', function () { return document.getElementById('r').getBBox().constructor.name; });
P('is a fresh object', function () {
  var r = document.getElementById('r');
  return r.getBBox() === r.getBBox();
});

P('stroke is not the box', function () {
  var r = document.getElementById('r');
  r.setAttribute('stroke', 'black');
  r.setAttribute('stroke-width', '20');
  return box('r');
});
P('own transform excluded', function () {
  var r = document.getElementById('r');
  r.setAttribute('transform', 'translate(100,100)');
  return box('r');
});
P('children transforms included', function () {
  var r = document.getElementById('r');
  r.setAttribute('transform', 'translate(100,100)');
  var answer = box('g');
  r.removeAttribute('transform');
  return answer;
});
P('container transform excluded', function () {
  document.getElementById('g').setAttribute('transform', 'translate(50,50) scale(3)');
  var answer = box('g');
  document.getElementById('g').removeAttribute('transform');
  return answer;
});
P('rotate maps to the corners', function () {
  var gg = document.createElementNS(SVG_NS, 'g');
  var rect = document.createElementNS(SVG_NS, 'rect');
  rect.setAttribute('x', '0'); rect.setAttribute('y', '0');
  rect.setAttribute('width', '10'); rect.setAttribute('height', '10');
  rect.setAttribute('transform', 'rotate(45)');
  gg.appendChild(rect);
  return attached(gg, boxOf);
});
P('viewBox does not scale the box', function () { return box('r'); });
P('percentage is of the viewport', function () {
  var r = document.getElementById('r');
  r.setAttribute('width', '50%');
  var answer = box('r');
  r.setAttribute('width', '3');
  return answer;
});
P('zero width draws nothing', function () {
  var n = document.createElementNS(SVG_NS, 'rect');
  n.setAttribute('x', '5'); n.setAttribute('y', '5');
  n.setAttribute('width', '0'); n.setAttribute('height', '4');
  return attached(n, boxOf);
});
P('empty path is empty', function () {
  var n = document.createElementNS(SVG_NS, 'path');
  return attached(n, boxOf);
});
P('curve bounds not control points', function () {
  var n = document.createElementNS(SVG_NS, 'path');
  n.setAttribute('d', 'M0 0 C0 100 100 100 100 0');
  return attached(n, function (e) {
    return boxOf(e).map(function (v) { return Math.round(v * 100) / 100; });
  });
});
P('quadratic bounds', function () {
  var n = document.createElementNS(SVG_NS, 'path');
  n.setAttribute('d', 'M0 0 Q50 100 100 0');
  return attached(n, function (e) {
    return boxOf(e).map(function (v) { return Math.round(v * 100) / 100; });
  });
});
P('arc bounds', function () {
  var n = document.createElementNS(SVG_NS, 'path');
  n.setAttribute('d', 'M0 0 A10 10 0 0 1 20 0');
  return attached(n, function (e) {
    return boxOf(e).map(function (v) { return Math.round(v * 100) / 100; });
  });
});
P('defs contributes nothing', function () {
  var defs = document.createElementNS(SVG_NS, 'defs');
  var rect = document.createElementNS(SVG_NS, 'rect');
  rect.setAttribute('x', '500'); rect.setAttribute('y', '500');
  rect.setAttribute('width', '10'); rect.setAttribute('height', '10');
  defs.appendChild(rect);
  var g = document.createElementNS(SVG_NS, 'g');
  g.appendChild(defs);
  return attached(g, boxOf);
});
P('units are user units', function () {
  var n = document.createElementNS(SVG_NS, 'rect');
  n.setAttribute('x', '1cm'); n.setAttribute('y', '0');
  n.setAttribute('width', '1cm'); n.setAttribute('height', '1mm');
  return attached(n, boxOf);
});
P('nested groups accumulate', function () {
  var outer = document.createElementNS(SVG_NS, 'g');
  var inner = document.createElementNS(SVG_NS, 'g');
  inner.setAttribute('transform', 'translate(10,10)');
  var rect = document.createElementNS(SVG_NS, 'rect');
  rect.setAttribute('width', '5'); rect.setAttribute('height', '5');
  inner.appendChild(rect);
  outer.appendChild(inner);
  return attached(outer, function (o) { return [boxOf(o), boxOf(inner)]; });
});
// `defs` is an SVGElement, not an SVGGraphicsElement, and still answers
// getBBox in a browser: SVG 2 puts the member on SVGGraphicsElement, and a
// browser's SVGGraphicsElement is what every SVG element object really is. The
// engine follows the specification's interface table instead, which is the
// documented difference -- asserted here so it stays deliberate.
P('getBBox is on the graphics element only', function () {
  var n = document.createElementNS(SVG_NS, 'defs');
  return [typeof n.getBBox];
});
console.log(out.join('\n'));
'''

BROWSER = r'''
const puppeteer=require('puppeteer-core'), chromium=require('@sparticuz/chromium');
(async()=>{const b=await puppeteer.launch({executablePath:await chromium.executablePath(),args:chromium.args,headless:true});
try { const page=await b.newPage(); let lines=[];
// The answers arrive as one console line; splitting on the marker keeps only
// this script's own, since a page can make Chromium log parse errors too.
page.on('console',m=>{ const t=m.text(); if (!t.includes('PROBE ')) return;
  for (const part of t.split('PROBE ').slice(1)) lines.push(part.replace(/\n$/, '')); });
await page.setContent(process.env.SVG_HTML); await page.evaluate(process.env.SVG_SCRIPT);
await new Promise(r=>setTimeout(r,150)); process.stdout.write(JSON.stringify(lines)); }
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
    with tempfile.TemporaryDirectory(prefix='svg-bbox-') as tmp:
        page, script = Path(tmp) / 'page.html', Path(tmp) / 'probe.js'
        page.write_text(HTML)
        script.write_text(SCRIPT)
        run = subprocess.run([str(ROOT / args.binary), '--run-script', str(script), str(page)],
                             cwd=ROOT, capture_output=True, text=True, timeout=120)
    if run.returncode:
        print(f'Litebird exited {run.returncode}: {run.stderr}')
        return 1
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
    print(f'SVG getBBox: {good}/{len(expected)} match Chromium')
    return int(good != len(expected) or len(actual) != len(expected))


if __name__ == '__main__':
    raise SystemExit(main())
