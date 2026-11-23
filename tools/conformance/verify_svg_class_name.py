#!/usr/bin/env python3
"""Compare SVG className behavior with pinned headless Chromium.

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
HTML = '<!doctype html><svg id="s"><rect id="r" class="first"/></svg><div id="h" class="html"></div>'
SCRIPT = r'''
var svg = document.getElementById('s'), el = document.getElementById('r');
var html = document.getElementById('h');
function check(name, f) { try { console.log(name + ': ' + JSON.stringify(f())); } catch (e) { console.log(name + ': THROWS ' + e.name); } }
check('initial', function() { return [typeof el.className, el.className.baseVal, el.className.animVal, svg.className.baseVal]; });
check('identity and brand', function() { return [el.className === el.className, el.className instanceof SVGAnimatedString, el.className.constructor.name]; });
check('attribute live', function() { var a=el.className; el.setAttribute('class', 'second'); return [a.baseVal, a.animVal, el.classList.contains('second')]; });
check('base writes', function() { var a=el.className; a.baseVal='third fourth'; return [el.getAttribute('class'), a.animVal, el.classList.contains('fourth')]; });
check('classList live', function() { el.classList.add('fifth'); return [el.className.baseVal, el.className.animVal]; });
check('attribute removal', function() { el.removeAttribute('class'); return [el.className.baseVal, el.className.animVal, el.hasAttribute('class')]; });
check('conversion', function() { el.className.baseVal=42; return [el.getAttribute('class'), el.className.baseVal]; });
check('null conversion', function() { el.className.baseVal=null; return [el.getAttribute('class'), el.className.baseVal]; });
check('read-only animated', function() { var a=el.className; a.animVal='bad'; return [a.baseVal,a.animVal]; });
check('read-only className', function() { var a=el.className; el.className='bad'; return [el.className===a,el.getAttribute('class')]; });
check('html unaffected', function() { html.className='next'; return [typeof html.className,html.className,html.getAttribute('class')]; });
check('illegal accessors', function() { var p=SVGAnimatedString.prototype; var g=Object.getOwnPropertyDescriptor(p,'baseVal').get; try { g.call({}); return 'no error'; } catch(e) { return e.name; } });
check('illegal constructor', function() { try { new SVGAnimatedString(); return 'no error'; } catch(e) { return e.name; } });
check('createElementNS', function() { var x=document.createElementNS('http://www.w3.org/2000/svg','g'); return [typeof x.className, x.className.baseVal, x.className===x.className]; });
check('selector after write', function() { el.className.baseVal='picked'; return [el.matches('.picked'),document.querySelector('.picked')===el]; });
check('illegal SVG getter', function() { var g=Object.getOwnPropertyDescriptor(SVGElement.prototype,'className').get; try { g.call(html); return 'no error'; } catch(e) { return e.name; } });
check('setter descriptor', function() { var d=Object.getOwnPropertyDescriptor(SVGAnimatedString.prototype,'animVal'); return [typeof d.get, typeof d.set, d.enumerable, d.configurable]; });
check('distinct and detached', function() { var n=el.cloneNode(false), v=n.className; n.remove(); v.baseVal='detached'; return [n.getAttribute('class'),v.animVal,v!==el.className]; });
check('strict readonly', function() { try { (function(){ 'use strict'; el.className='bad'; })(); return 'no error'; } catch(e) { return e.name; } });
check('string tag', function() { return Object.prototype.toString.call(el.className); });
'''
BROWSER = r'''
const puppeteer=require('puppeteer-core'), chromium=require('@sparticuz/chromium');
(async()=>{const b=await puppeteer.launch({executablePath:await chromium.executablePath(),args:chromium.args,headless:true});
try { const page=await b.newPage(); let lines=[]; page.on('console',m=>lines.push(m.text()));
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
    browser = subprocess.run(['node', '-e', BROWSER], cwd=ROOT, env=env, capture_output=True, text=True, timeout=60, check=True)
    expected = json.loads(browser.stdout)
    names = ('initial', 'identity and brand', 'attribute live', 'base writes',
             'classList live', 'attribute removal', 'conversion', 'null conversion',
             'read-only animated', 'read-only className', 'html unaffected',
             'illegal accessors', 'illegal constructor', 'createElementNS',
             'selector after write', 'illegal SVG getter', 'setter descriptor',
             'distinct and detached', 'strict readonly', 'string tag')
    if len(expected) != len(names) or any(not line.startswith(name + ': ') for name, line in zip(names, expected)):
        raise ValueError('Chromium reference did not return all 20 named answers')
    with tempfile.TemporaryDirectory(prefix='svg-class-') as tmp:
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
    print(f'SVG className: {good}/{len(expected)} match Chromium')
    return int(good != len(expected) or len(actual) != len(expected))

if __name__ == '__main__':
    raise SystemExit(main())
