#!/usr/bin/env python3
"""Compare SVG viewBox behavior with pinned headless Chromium.

Install the browser once as in the phase document, then run this script. The
reference and Litebird execute the same independent page script; mismatches
are printed individually, and any mismatch makes the exit status nonzero.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HTML = '<!doctype html><svg id="s" viewBox="1 2 30 40"></svg><div id="h"></div>'
SCRIPT = r'''
var s=document.getElementById('s'), a=s.viewBox, r=a && a.baseVal, v=a && a.animVal;
function check(name, f) { try { console.log(name + ': ' + JSON.stringify(f())); } catch(e) { console.log(name + ': THROWS ' + e.name); } }
function values(x) { return [x.x,x.y,x.width,x.height]; }
function err(f) { try { f(); return 'no error'; } catch(e) { return e.name; } }
check('brand', function() { return [a.constructor.name,r.constructor.name,a instanceof SVGAnimatedRect,r instanceof SVGRect,Object.prototype.toString.call(r)]; });
check('identity', function() { return [a===s.viewBox,r===a.baseVal,v===a.animVal,r===v]; });
check('initial', function() { return [values(r),values(v)]; });
check('attribute live', function() { s.setAttribute('viewBox','-1, 2, 50, 60'); return [values(r),values(v)]; });
check('base write', function() { r.x=10; r.height=15; return [s.getAttribute('viewBox'),values(r),values(v)]; });
check('negative dimensions', function() { r.width=-3; return [s.getAttribute('viewBox'),r.width,v.width]; });
check('remove attribute', function() { s.removeAttribute('viewBox'); return [values(r),values(v),s.hasAttribute('viewBox')]; });
check('write absent', function() { r.y=9; return [s.getAttribute('viewBox'),values(r)]; });
check('invalid attributes', function() { var out=[]; ['1 2 3','1 2 3 4 5','1,,2 3 4','1 2 3 4,','NaN 2 3 4','1e999 2 3 4'].forEach(function(x){s.setAttribute('viewBox',x);out.push(values(r));}); return out; });
check('number syntax', function() { var out=[]; ['1-2 3 4','1.2.3 4 5','1e2 -.5 3 4'].forEach(function(x){s.setAttribute('viewBox',x);out.push(values(r));});return out; });
check('read only anim', function() { var before=s.getAttribute('viewBox'); return [err(function(){v.x=8;}),s.getAttribute('viewBox')===before]; });
check('anim conversion first', function() { var touched=0; var e=err(function(){v.x={valueOf:function(){touched++;return 1;}}}); return [e,touched]; });
check('read only holders', function() { var old=a.baseVal; a.baseVal={}; s.viewBox={}; return [a.baseVal===old,s.viewBox===a]; });
check('number conversion', function() { r.x='2.5';r.y=null;return [values(r),s.getAttribute('viewBox')]; });
check('float precision', function() { r.x=1/3; return [r.x,v.x,s.getAttribute('viewBox')]; });
check('nonfinite', function() { r.x=Infinity;return [r.x===Infinity,s.getAttribute('viewBox')]; });
check('invalid decimal point', function() { s.setAttribute('viewBox','1. 2 3 4');return values(r); });
check('float underflow', function() { s.setAttribute('viewBox','1e-1000 2 3 4');return values(r); });
check('nan write', function() { r.x=NaN;return [Number.isNaN(r.x),s.getAttribute('viewBox')]; });
check('large write', function() { r.x=1e300;return [r.x===Infinity,s.getAttribute('viewBox')]; });
check('exponent serialization', function() { r.x=1e6;return [r.x,s.getAttribute('viewBox')]; });
check('small serialization', function() { r.x=1e-5;return s.getAttribute('viewBox'); });
check('strict readonly', function() { return [err(function(){(function(){'use strict';s.viewBox=4;})();}),err(function(){(function(){'use strict';a.animVal=4;})();})]; });
check('conversion reentrancy', function() { r.y={valueOf:function(){s.setAttribute('viewBox','4 5 6 7');return 9;}};return [values(r),s.getAttribute('viewBox')]; });
check('same-text attribute update', function() { r.x=1/3;var text=s.getAttribute('viewBox');s.setAttribute('viewBox',text);return [r.x,s.getAttribute('viewBox')]; });
check('unrelated attribute update', function() { r.x=1/3;s.setAttribute('data-test','x');return [r.x,s.getAttribute('viewBox')]; });
check('attribute node update', function() { var attr=s.getAttributeNode('viewBox');r.x=1/3;attr.value=s.getAttribute('viewBox');return [r.x,s.getAttribute('viewBox')]; });
check('decimal accumulation', function() { var out=[]; ['0.000001','0.333333','1.333333','-0.333333','0.333333e2','16777217','1e-5','3.4028235e38'].forEach(function(n){s.setAttribute('viewBox',n+' 2 3 4');out.push(r.x);});return out; });
check('serialization boundaries', function() { var out=[];[0.0001,0.000001,0.0000001,999999.9,-0,0.12345678,1e20,-123.456].forEach(function(x){r.x=x;out.push(s.getAttribute('viewBox'));});return out; });
check('namespace isolation', function() { r.x=1/3;var x=r.x;s.setAttributeNS('urn:other','p:viewBox','10 20 30 40');return [r.x===x,s.getAttribute('viewBox')]; });
check('constructor', function() { return [err(function(){new SVGRect();}),err(function(){new SVGAnimatedRect();})]; });
check('bad receiver', function() { var g=Object.getOwnPropertyDescriptor(SVGSVGElement.prototype,'viewBox').get;var x=Object.getOwnPropertyDescriptor(SVGRect.prototype,'x').get;return [err(function(){g.call(document.getElementById('h'));}),err(function(){x.call({});})]; });
check('descriptor', function() { var d=Object.getOwnPropertyDescriptor(SVGAnimatedRect.prototype,'animVal');return [typeof d.get,typeof d.set,d.enumerable,d.configurable]; });
check('detached', function() { var e=document.createElementNS('http://www.w3.org/2000/svg','svg');var box=e.viewBox.baseVal;e.remove();box.width=12;return [e.getAttribute('viewBox'),box.width,e.viewBox.baseVal===box]; });
'''
BROWSER = r'''
const puppeteer=require('puppeteer-core'), chromium=require('@sparticuz/chromium');
(async()=>{const b=await puppeteer.launch({executablePath:await chromium.executablePath(),args:chromium.args,headless:true});
try { const page=await b.newPage(); let lines=[]; page.on('console',m=>{if(m.type()==='log')lines.push(m.text())});
await page.setContent(process.env.VIEWBOX_HTML); await page.evaluate(process.env.VIEWBOX_SCRIPT); process.stdout.write(JSON.stringify(lines)); }
finally { await b.close(); }})().catch(e=>{console.error(e);process.exit(1)});
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    args = p.parse_args()
    env = dict(os.environ, NODE_PATH=str(ROOT / 'build/svg-oracle/node_modules'),
               LD_LIBRARY_PATH=str(ROOT / 'build/svg-oracle/runtime/lib') + ':' + os.environ.get('LD_LIBRARY_PATH', ''),
               VIEWBOX_HTML=HTML, VIEWBOX_SCRIPT=SCRIPT)
    browser = subprocess.run(['node', '-e', BROWSER], cwd=ROOT, env=env, capture_output=True, text=True, timeout=60, check=True)
    expected = json.loads(browser.stdout)
    names = ('brand', 'identity', 'initial', 'attribute live', 'base write', 'negative dimensions', 'remove attribute', 'write absent', 'invalid attributes', 'number syntax', 'read only anim', 'anim conversion first', 'read only holders', 'number conversion', 'float precision', 'nonfinite', 'invalid decimal point', 'float underflow', 'nan write', 'large write', 'exponent serialization', 'small serialization', 'strict readonly', 'conversion reentrancy', 'same-text attribute update', 'unrelated attribute update', 'attribute node update', 'decimal accumulation', 'serialization boundaries', 'namespace isolation', 'constructor', 'bad receiver', 'descriptor', 'detached')
    if len(expected) != len(names) or any(not line.startswith(name + ": ") for name, line in zip(names, expected)):
        raise ValueError("Chromium reference did not return all answers")
    with tempfile.TemporaryDirectory(prefix='svg-viewbox-') as tmp:
        page, script = Path(tmp)/'page.html', Path(tmp)/'probe.js'
        page.write_text(HTML); script.write_text(SCRIPT)
        run = subprocess.run([str(ROOT / args.binary), '--run-script', str(script), str(page)],
                             cwd=ROOT, capture_output=True, text=True, timeout=60)
    if run.returncode:
        print(f'Litebird exited {run.returncode}: {run.stderr}'); return 1
    actual = run.stdout.splitlines()
    print(f'Chromium: {len(expected)} answers; Litebird: {len(actual)} answers')
    good = 0
    for i in range(max(len(expected), len(actual))):
        a = actual[i] if i < len(actual) else '<missing>'
        e = expected[i] if i < len(expected) else '<missing>'
        if a == e:
            good += 1; print('PASS', e)
        else:
            print('FAIL expected:', e, '\n     actual: ', a)
    print(f'SVG viewBox: {good}/{len(expected)} match Chromium')
    return int(good != len(expected) or len(actual) != len(expected))

if __name__ == '__main__':
    raise SystemExit(main())
