#!/usr/bin/env python3
"""Compare CSSOM font-face behavior with pinned headless Chromium.

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
HTML = '<!doctype html><p>CSSOM font face probe</p>'
SCRIPT = r'''
var s = new CSSStyleSheet();
s.replaceSync('@font-face { font-family: Example; src: local(Example); font-display: swap; } @media screen { @font-face { font-family: Nested; } }');
function check(name, f) { try { console.log(name + ': ' + JSON.stringify(f())); } catch (e) { console.log(name + ': THROWS ' + e.name); } }
check('rule count', function() { return s.cssRules.length; });
check('brand and type', function() { var r=s.cssRules[0]; return [r.constructor.name,r.type,r instanceof CSSFontFaceRule,r instanceof CSSRule]; });
check('rule identity', function() { return s.cssRules[0]===s.cssRules.item(0); });
check('declarations', function() { var st=s.cssRules[0].style; return [st.length,st.item(0),st.getPropertyValue('font-family'),st.getPropertyValue('font-display')]; });
check('style identity', function() { var r=s.cssRules[0]; return [r.style===r.style,r.style.parentRule===r]; });
check('nested rule', function() { var m=s.cssRules[1], r=m.cssRules[0]; return [r.type,r.parentRule===m,r.parentStyleSheet===s]; });
check('insert rule', function() { var t=new CSSStyleSheet(); return [t.insertRule('@font-face { font-family: Test; }',0),t.cssRules[0].type,t.cssRules.length]; });
check('delete rule', function() { var t=new CSSStyleSheet(); t.replaceSync('@font-face { font-family: Test; }'); t.deleteRule(0); return t.cssRules.length; });
check('set descriptor', function() { var st=s.cssRules[0].style; st.setProperty('font-display','block'); return st.getPropertyValue('font-display'); });
check('remove descriptor', function() { var st=s.cssRules[0].style; return [st.removeProperty('font-display'),st.getPropertyValue('font-display')]; });
check('invalid constructor', function() { try { new CSSFontFaceRule(); return 'no error'; } catch(e) { return e.name; } });
'''
BROWSER = r'''
const puppeteer=require('puppeteer-core'), chromium=require('@sparticuz/chromium');
(async()=>{const b=await puppeteer.launch({executablePath:await chromium.executablePath(),args:chromium.args,headless:true});
try { const page=await b.newPage(); let lines=[]; page.on('console',m=>lines.push(m.text()));
await page.setContent(process.env.FONT_HTML); await page.evaluate(process.env.FONT_SCRIPT); process.stdout.write(JSON.stringify(lines)); }
finally { await b.close(); }})().catch(e=>{console.error(e);process.exit(1)});
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary', default='build/litebird')
    args = p.parse_args()
    env = dict(os.environ, NODE_PATH=str(ROOT / 'build/svg-oracle/node_modules'),
               LD_LIBRARY_PATH=str(ROOT / 'build/svg-oracle/runtime/lib') + ':' + os.environ.get('LD_LIBRARY_PATH', ''),
               FONT_HTML=HTML, FONT_SCRIPT=SCRIPT)
    browser = subprocess.run(['node', '-e', BROWSER], cwd=ROOT, env=env, capture_output=True, text=True, timeout=60, check=True)
    expected = json.loads(browser.stdout)
    names = ('rule count', 'brand and type', 'rule identity', 'declarations', 'style identity', 'nested rule', 'insert rule', 'delete rule', 'set descriptor', 'remove descriptor', 'invalid constructor')
    if len(expected) != len(names) or any(not line.startswith(name + ": ") for name, line in zip(names, expected)):
        raise ValueError("Chromium reference did not return all answers")
    with tempfile.TemporaryDirectory(prefix='font-face-') as tmp:
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
    print(f'CSSOM font-face: {good}/{len(expected)} match Chromium')
    return int(good != len(expected) or len(actual) != len(expected))

if __name__ == '__main__':
    raise SystemExit(main())
