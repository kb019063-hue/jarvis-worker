#!/usr/bin/env python3
"""Runs one page and one script in jsdom and in Litebird and shows where they differ.

The DOM corpus (tests/dom-conformance) answers "does Litebird still do what it
did"; it cannot answer "what does it not do", because its cases were written
by whoever wrote the code they test. At 557 of 557, getComputedStyle answered
"" for every property nobody had declared, and a Range did not follow the
tree: no case had asked. Both were found by running things that were not in
the corpus through this tool, and that is what it is for.

Usage:
    python3 tools/conformance/compare_with_jsdom.py page.html script.js
    python3 tools/conformance/compare_with_jsdom.py --html '<p id=p>x</p>' script.js

The script prints lines (console.log). Every line that differs is shown with
both answers; the last line counts them. Exit status 1 when any differ.

A difference is a question, not a verdict. jsdom is wrong too, in places:
check the standard before changing Litebird, and when Litebird is the one
that is wrong, add a case to the corpus with `run_dom_conformance.py
--record` so that the answer stays.

Needs Node.js and build/jsdom (run_dom_conformance.py --reference installs it).
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGE_URL = "https://example.test/app/page.html?tab=2#top"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("page", nargs="?", help="an HTML file")
    parser.add_argument("script", help="a JavaScript file that prints lines")
    parser.add_argument("--html", help="the page as text, instead of a file")
    parser.add_argument("--binary", default=os.path.join(REPO, "build", "litebird"))
    parser.add_argument("--jsdom-dir", default=os.path.join(REPO, "build", "jsdom"))
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--width", type=int, default=150, help="characters of each answer to show")
    args = parser.parse_args()
    if (args.page is None) == (args.html is None):
        parser.error("give a page file or --html, not both and not neither")
    # newline="" everywhere, and Litebird's output read as bytes: a carriage
    # return in the page, in the script or in an answer is what the question
    # is about often enough (XML turns it into a line feed, a serializer
    # writes it), and text mode would turn every one into a line feed.
    html = args.html if args.html is not None else open(args.page, encoding="utf-8", newline="").read()
    code = open(args.script, encoding="utf-8", newline="").read()

    request = json.dumps({"url": PAGE_URL, "max_ms": 5000, "cases": [{"html": html, "code": code}]})
    runner = os.path.join(REPO, "tools", "conformance", "jsdom_run.mjs")
    reference = subprocess.run(["node", runner, args.jsdom_dir], input=request.encode("utf-8"), capture_output=True)
    try:
        answer = json.loads(reference.stdout.decode("utf-8"))[0]
    except (ValueError, IndexError):
        sys.exit("jsdom did not run: " + (reference.stderr or reference.stdout).decode("utf-8", "replace").strip()[:400])
    if answer.get("error"):
        print("jsdom:    ", answer["error"])
    theirs = answer["output"].rstrip("\n").split("\n")

    with tempfile.TemporaryDirectory() as scratch:
        page = os.path.join(scratch, "page.html")
        script = os.path.join(scratch, "script.js")
        with open(page, "w", encoding="utf-8", newline="") as out:
            out.write(html)
        with open(script, "w", encoding="utf-8", newline="") as out:
            out.write(code)
        run = subprocess.run([args.binary, "--url=" + PAGE_URL, "--run-script", script, page],
                             capture_output=True, timeout=args.timeout)
    complaint = run.stderr.decode("utf-8", "replace").strip()
    if run.returncode or complaint:
        print("litebird: exit %d %s" % (run.returncode, complaint[:400]))
    ours = run.stdout.decode("utf-8", "replace").rstrip("\n").split("\n")

    same = 0
    total = max(len(theirs), len(ours))
    for index in range(total):
        expected = theirs[index] if index < len(theirs) else "<no line>"
        actual = ours[index] if index < len(ours) else "<no line>"
        if expected == actual:
            same += 1
            continue
        # repr: a carriage return or another control character is seen.
        print("  jsdom     " + repr(expected[:args.width])[1:-1])
        print("  litebird  " + repr(actual[:args.width])[1:-1])
    print("%d lines, %d the same" % (total, same))
    return 0 if same == total else 1


if __name__ == "__main__":
    sys.exit(main())
