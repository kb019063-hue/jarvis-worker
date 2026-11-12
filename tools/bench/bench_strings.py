#!/usr/bin/env python3
"""Times string-heavy scripts in Litebird, and in a reference engine when one is installed.

Every script here does work that grows in step with its input: read each
character of a long string once, append to a string many times, pull tokens
off the front of a text. They are the patterns that used to be quadratic in
Litebird (AUDIT.md, finding 25), so a case that takes seconds here is a cliff,
not a slow interpreter. Each script prints a checksum, and the two engines
must print the same one.

Usage:
    python3 tools/bench/bench_strings.py                    # every case
    python3 tools/bench/bench_strings.py --only append      # cases whose name contains "append"
    python3 tools/bench/bench_strings.py --reps 3           # best of 3
    python3 tools/bench/bench_strings.py --max-ms 3000      # exit 1 if any case is slower or wrong
    python3 tools/bench/bench_strings.py --reference ""     # do not run a reference engine

Times are for the whole process, start-up included (about 5 ms for Litebird,
30 ms for Node).
"""
import argparse
import shutil
import subprocess
import sys
import time

ASCII = "'abcdefghij'"
MIXED = r"'ab\u00e9\u4e2d\ud83d\ude00'"      # a, b, e-acute, a CJK character, an emoji (two code units)

CASES = [
    # --- reading ----------------------------------------------------------------
    ("read: charCodeAt, 200k ASCII",
     f"var s={ASCII}.repeat(20000),t=0;for(var i=0;i<s.length;i++)t+=s.charCodeAt(i);console.log(t)"),
    ("read: s[i], 200k ASCII",
     f"var s={ASCII}.repeat(20000),t=0;for(var i=0;i<s.length;i++)if(s[i]==='c')t++;console.log(t)"),
    ("read: charAt, 200k ASCII",
     f"var s={ASCII}.repeat(20000),t=0;for(var i=0;i<s.length;i++)if(s.charAt(i)==='c')t++;console.log(t)"),
    ("read: charCodeAt, 200k non-ASCII",
     f"var s={MIXED}.repeat(33334),t=0;for(var i=0;i<s.length;i++)t+=s.charCodeAt(i);console.log(s.length,t)"),
    ("read: s[i], 200k non-ASCII",
     f"var s={MIXED}.repeat(33334),t=0;for(var i=0;i<s.length;i++)if(s[i]==='\\u4e2d')t++;console.log(t)"),
    ("read: .length as loop bound, non-ASCII",
     r"var s='\u00e9\u4e2dxyz'.repeat(40000),t=0;for(var i=0;i<s.length;i++)t++;console.log(t)"),
    ("read: for-of, 200k non-ASCII",
     f"var s={MIXED}.repeat(33334),t=0;for(var ch of s)t+=ch.length;console.log(t)"),
    ("read: long string through 200k calls",
     f"var s={ASCII}.repeat(20000);function f(x,i){{return x.charCodeAt(i)}}var t=0;"
     "for(var i=0;i<200000;i++)t+=f(s,i);console.log(t)"),

    # --- taking pieces ----------------------------------------------------------
    ("piece: substring(i, i+5), 100k ASCII",
     f"var s={ASCII}.repeat(10000),t=0;for(var i=0;i+5<=s.length;i++)t+=s.substring(i,i+5).length;console.log(t)"),
    ("piece: substring(i, i+5), 100k non-ASCII",
     f"var s={MIXED}.repeat(16667),t=0;for(var i=0;i+5<=s.length;i++)t+=s.substring(i,i+5).length;console.log(t)"),
    ("piece: indexOf walk, 20k hits",
     "var s='abcdefghi,'.repeat(20000),n=0,p=0;while((p=s.indexOf(',',p))!==-1){n++;p++}console.log(n)"),
    ("piece: indexOf walk, non-ASCII",
     r"var s='caf\u00e9 \u4e2d\u6587,'.repeat(20000),n=0,p=0;while((p=s.indexOf(',',p))!==-1){n++;p++}console.log(n)"),
    ("piece: split + join, 200k parts",
     "var s='ab,cd,ef,gh,'.repeat(50000);var p=s.split(',');console.log(p.length,p.join(';').length)"),
    ("piece: split(''), 100k non-ASCII",
     f"var s={MIXED}.repeat(16667);var p=s.split('');console.log(p.length,p.reverse().join('').length)"),

    # --- building ---------------------------------------------------------------
    ("build: s += x, 100k times",
     "var s='';for(var i=0;i<100000;i++)s+='item'+(i%10)+',';console.log(s.length)"),
    ("build: s = s + x, 100k times",
     "var s='';for(var i=0;i<100000;i++)s=s+'ab';console.log(s.length)"),
    ("build: obj.s += x, 100k times",
     "var o={s:''};for(var i=0;i<100000;i++)o.s+='abc';console.log(o.s.length)"),
    ("build: s += non-ASCII, 100k times",
     r"var s='';for(var i=0;i<100000;i++)s+='\u00e9\u4e2d'+(i%10);console.log(s.length)"),
    ("build: html rows, 30k",
     "var h='<table>';for(var i=0;i<30000;i++){h+='<tr><td>'+i+'</td><td class=\"n\">'+(i*2)+'</td></tr>'}"
     "h+='</table>';console.log(h.length)"),
    ("build: append, then read the end, 50k",
     "var s='',t=0;for(var i=0;i<50000;i++){s+='ab';t+=s.charCodeAt(s.length-1)}console.log(t)"),
    ("build: append, then read the end, non-ASCII",
     r"var s='',t=0;for(var i=0;i<50000;i++){s+='\u00e9b';t+=s.charCodeAt(s.length-2)}console.log(t)"),
    ("build: append, keep every prefix, 20k",
     "var s='',all=[];for(var i=0;i<20000;i++){s+='abcdefgh';all.push(s)}"
     "var t=0;for(var i=0;i<all.length;i+=997)t+=all[i].length;console.log(t,all[19999].length)"),
    ("build: push + join, 100k",
     "var a=[];for(var i=0;i<100000;i++)a.push('item'+(i%10));console.log(a.join(',').length)"),
    ("build: prepend, 20k times",
     "var s='';for(var i=0;i<20000;i++)s='ab'+s;console.log(s.length)"),

    # --- consuming --------------------------------------------------------------
    ("consume: s = s.slice(k), 20k tokens",
     "var s='token,'.repeat(20000),n=0;while(s.length){var k=s.indexOf(',');s=s.slice(k+1);n++}console.log(n)"),
    ("consume: s = s.slice(k), non-ASCII",
     r"var s='t\u00f6ken,'.repeat(20000),n=0;while(s.length){var k=s.indexOf(',');s=s.slice(k+1);n++}console.log(n)"),
    ("consume: regex at the front, 10k tokens",
     "var s='word 123 '.repeat(5000),n=0,m;while(s.length){m=/^(\\w+)\\s*/.exec(s);s=s.substring(m[0].length);n++}"
     "console.log(n)"),

    # --- regular expressions ----------------------------------------------------
    ("regex: exec loop, 20k matches",
     "var s='key=value;'.repeat(20000),re=/(\\w+)=(\\w+);/g,m,n=0;while((m=re.exec(s)))n+=m[1].length;console.log(n)"),
    ("regex: exec loop, non-ASCII subject",
     r"var s='cl\u00e9=val\u00fcr;'.repeat(20000),re=/([^=;]+)=([^=;]+);/g,m,n=0;"
     "while((m=re.exec(s)))n+=m[1].length;console.log(n)"),
    ("regex: sticky tokenizer, 150k characters",
     "var s='let x1 = 42 + y;\\n'.repeat(9000),re=/\\s*([A-Za-z_]\\w*|\\d+|[=+;])/y,m,n=0;"
     "while((m=re.exec(s)))n++;console.log(n)"),
    ("regex: replace /g on 240k",
     "var s='hello world '.repeat(20000);console.log(s.replace(/o/g,'0').length,s.split(' ').length)"),
    ("regex: test() on a long string, 20k times",
     "var s='x'.repeat(200000)+'end',t=0;for(var i=0;i<20000;i++)if(/^x/.test(s))t++;console.log(t)"),
    ("regex: lookbehind, 20k matches",
     "var s='ab,cd;'.repeat(20000),re=/(?<=,)\\w+/g,n=0;while(re.exec(s))n++;console.log(n)"),
    ("regex: negative lookbehind, no match in 120k",
     "var s='ab,cd;'.repeat(20000),re=/(?<!,)c/g,n=0;while(re.exec(s))n++;console.log(n)"),

    # --- short strings: these must not get slower -------------------------------
    ("short: 'k' + i, 200k",
     "var t=0;for(var i=0;i<200000;i++){var s='k'+i;t+=s.length}console.log(t)"),
    ("short: === on short strings, 300k",
     "var a=['red','green','blue','alpha'],t=0;for(var i=0;i<300000;i++){if(a[i&3]==='blue')t++}console.log(t)"),
    ("short: typeof x === 'string', 300k",
     "var a=['s',1,{},null],t=0;for(var i=0;i<300000;i++){if(typeof a[i&3]==='string')t++}console.log(t)"),
    ("short: string keys, 200k writes",
     "var o={},t=0;for(var i=0;i<200000;i++){o['k'+(i%500)]=i}for(var k in o)t+=o[k];console.log(t)"),
    ("short: sort 50k strings",
     "var a=[];for(var i=0;i<50000;i++)a.push('k'+((i*7919)%50000));a.sort();console.log(a[0],a[49999])"),
]


def run(command, code, timeout):
    start = time.perf_counter()
    try:
        proc = subprocess.run(command + [code], capture_output=True, text=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "timeout"
    elapsed = time.perf_counter() - start
    out = " | ".join(proc.stdout.strip().splitlines())[:48]
    if proc.returncode != 0:
        out = f"exit {proc.returncode}: {out}"
    return elapsed, out


def best_of(command, code, timeout, reps):
    best, out = None, ""
    for _ in range(reps):
        elapsed, out = run(command, code, timeout)
        if elapsed is None:
            return None, out
        best = elapsed if best is None else min(best, elapsed)
    return best, out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--binary", default="build/litebird")
    ap.add_argument("--reference", default="node -e",
                    help="reference engine command; the script is appended (default: 'node -e'). Empty: none")
    ap.add_argument("--only", default="", help="run cases whose name contains this")
    ap.add_argument("--timeout", type=float, default=20.0, help="seconds per case (default 20)")
    ap.add_argument("--reps", type=int, default=1, help="repetitions; the best time is reported")
    ap.add_argument("--max-ms", type=float, default=0.0,
                    help="fail when a Litebird case takes longer than this, times out or prints a different result")
    args = ap.parse_args()

    reference = args.reference.split()
    if reference and not shutil.which(reference[0]):
        print(f"note: {reference[0]} is not installed; running Litebird only", file=sys.stderr)
        reference = []

    failures = 0
    print(f"{'case':44} {'litebird':>10} {'reference':>10}  result")
    for name, code in CASES:
        if args.only and args.only not in name:
            continue
        elapsed, out = best_of([args.binary, "--run-js"], code, args.timeout, args.reps)
        shown = f"{elapsed * 1000:8.0f}ms" if elapsed is not None else f"  >{args.timeout:.0f}s"
        ref_shown, note = "", ""
        if reference:
            ref_elapsed, ref_out = best_of(reference, code, 60.0, args.reps)
            ref_shown = f"{ref_elapsed * 1000:8.0f}ms" if ref_elapsed is not None else "timeout"
            if elapsed is not None and out != ref_out:
                note = f"   MISMATCH, reference printed: {ref_out}"
                failures += 1
        if elapsed is None or out.startswith("exit ") or (args.max_ms and elapsed * 1000 > args.max_ms):
            if not note:
                note = "   TOO SLOW" if elapsed is None or not out.startswith("exit ") else "   FAILED"
            failures += 1
        print(f"{name:44} {shown:>10} {ref_shown:>10}  {out}{note}", flush=True)

    if failures and args.max_ms:
        print(f"\n{failures} case(s) failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
