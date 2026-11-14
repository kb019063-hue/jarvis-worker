#!/usr/bin/env python3
"""Runs the W3C XML Conformance Test Suite against Litebird's XML parser.

The suite (tests/xml/xmlts20130923.zip, the W3C's archive as published) is
2,585 documents contributed by Sun, OASIS/NIST, IBM, Fuji Xerox, James Clark
and the University of Edinburgh. Each says whether it is well formed, and many
come with the canonical form a parser must be able to write back.

A browser's XML parser does not validate, is aware of namespaces, reads no
external entity and follows XML 1.0 Fifth Edition. So of the suite:

  counted, "must accept"   TYPE valid or invalid: well formed either way.
                           With an OUTPUT file, the canonical form must match.
  counted, "must reject"   TYPE not-wf.
  not counted              XML 1.1 and Namespaces 1.1; tests for the editions
                           before the fifth where the fifth differs; tests
                           whose result needs an external entity to be read;
                           documents that are not namespace-well-formed on
                           purpose (NAMESPACE="no"); TYPE error (a parser may
                           report those or not); encodings this build cannot
                           read (the Japanese ones).

Every document is still run, counted or not: none may crash the parser or run
out of time.

Usage:
    python3 tools/conformance/run_xml_conformance.py [options]

Options:
    --binary PATH     Litebird binary (default: build/litebird)
    --suite PATH      The archive (default: tests/xml/xmlts20130923.zip)
    --filter SUBSTR   Only tests whose ID or path contains SUBSTR
    --show N          Print up to N failing tests
    --jobs N          Parallel processes (default: os.cpu_count())
    --timeout SECS    Per-test timeout (default: 20)
    --quiet           Only the summary lines
    --json PATH       Write a machine-readable summary
    --min-pass N      Exit 1 if fewer than N counted tests pass (the CI floor)
    --reference       Run Expat (Python's) and, when installed, libxml2 (lxml)
                      over the counted tests instead, and say how many each
                      passes. A check on the counting, not on Litebird.
"""
import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import zipfile
import xml.parsers.expat as expat

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def unpack(archive, into):
    stamp = os.path.join(into, ".unpacked")
    if os.path.exists(stamp) and os.path.getmtime(stamp) >= os.path.getmtime(archive):
        return os.path.join(into, "xmlconf")
    os.makedirs(into, exist_ok=True)
    with zipfile.ZipFile(archive) as z:
        z.extractall(into)
    with open(stamp, "w") as out:
        out.write("ok\n")
    return os.path.join(into, "xmlconf")


def read_catalogs(root):
    """Every TEST element of the suite, as a dict of its attributes."""
    master = open(os.path.join(root, "xmlconf.xml"), encoding="utf-8").read()
    tests = []
    for catalog in re.findall(r'<!ENTITY\s+\S+\s+SYSTEM\s+"([^"]+)"', master):
        path = os.path.join(root, catalog)
        base = os.path.dirname(path)
        bases = [base]
        current = None

        def start(name, attributes):
            nonlocal current
            if name == "TESTCASES":
                extra = attributes.get("xml:base")
                bases.append(os.path.normpath(os.path.join(base, extra)) if extra else bases[-1])
            elif name == "TEST":
                current = dict(attributes)
                current["catalog"] = catalog
                current["path"] = os.path.normpath(os.path.join(bases[-1], attributes["URI"]))
                current["output"] = (os.path.normpath(os.path.join(bases[-1], attributes["OUTPUT"]))
                                     if attributes.get("OUTPUT") else None)
                current["description"] = ""

        def end(name):
            nonlocal current
            if name == "TESTCASES":
                bases.pop()
            elif name == "TEST":
                current["description"] = " ".join(current["description"].split())
                tests.append(current)
                current = None

        def text(data):
            if current is not None:
                current["description"] += data

        parser = expat.ParserCreate()
        parser.StartElementHandler = start
        parser.EndElementHandler = end
        parser.CharacterDataHandler = text
        body = open(path, "rb").read()
        # A catalog is an external entity: no single root, maybe a text declaration.
        body = re.sub(rb"^\s*<\?xml[^>]*\?>", b"", body)
        parser.Parse(b"<R>" + body + b"</R>", True)
    return tests


def classify(test):
    """('accept'|'reject', None) for a counted test, (None, reason) otherwise."""
    recommendation = test.get("RECOMMENDATION", "XML1.0")
    if "1.1" in recommendation.replace("XML1.0", "").replace("NS1.0", "") or test.get("VERSION") == "1.1":
        return None, "XML 1.1"
    edition = test.get("EDITION", "")
    if edition and "5" not in edition.split():
        return None, "an edition before the fifth"
    if test["TYPE"] == "error":
        return None, "an error a parser may report or not"
    if test.get("NAMESPACE", "yes") == "no":
        return None, "not namespace-well-formed on purpose"
    if test.get("ENTITIES", "none") != "none":
        return None, "needs an external entity"
    return ("reject" if test["TYPE"] == "not-wf" else "accept"), None


def run_litebird(binary, test, timeout):
    try:
        done = subprocess.run([binary, "--xml-canonical", test["path"]], capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "timeout", b"", ""
    message = done.stderr.decode("utf-8", "replace").strip()
    if done.returncode == 0:
        return "accept", done.stdout, message
    if done.returncode == 1:
        return "reject", b"", message
    if done.returncode == 3:
        return "encoding", b"", message
    return "crash", b"", "exit status %d %s" % (done.returncode, message[-300:])


def run_expat(test):
    # The separator is a character no XML text can hold: Expat refuses a
    # namespace name with the separator in it.
    parser = expat.ParserCreate(namespace_separator="\x01")
    parser.SetParamEntityParsing(expat.XML_PARAM_ENTITY_PARSING_ALWAYS)
    parser.ExternalEntityRefHandler = lambda *a: 1      # nothing is fetched, as in a browser
    try:
        parser.Parse(open(test["path"], "rb").read(), True)
        return "accept"
    except (expat.ExpatError, ValueError, LookupError):
        return "reject"


def run_lxml(test):
    from lxml import etree
    options = etree.XMLParser(resolve_entities=True, load_dtd=False, no_network=True, dtd_validation=False)
    try:
        etree.fromstring(open(test["path"], "rb").read(), options)
        return "accept"
    except etree.XMLSyntaxError:
        return "reject"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=os.path.join(REPO, "build", "litebird"))
    parser.add_argument("--suite", default=os.path.join(REPO, "tests", "xml", "xmlts20130923.zip"))
    parser.add_argument("--filter", default="")
    parser.add_argument("--show", type=int, default=0)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--timeout", type=float, default=20)
    parser.add_argument("--quiet", action="store_true")
    parser.add_argument("--json")
    parser.add_argument("--min-pass", type=int, default=0)
    parser.add_argument("--reference", action="store_true")
    args = parser.parse_args()

    root = unpack(args.suite, os.path.join(REPO, "build", "xml-conformance"))
    tests = [t for t in read_catalogs(root) if args.filter in t["ID"] or args.filter in t["path"]]

    if args.reference:
        counted = [(t, classify(t)[0]) for t in tests if classify(t)[0]]
        references = [("Expat " + expat.EXPAT_VERSION.replace("expat_", ""), run_expat)]
        try:
            from lxml import etree
            references.append(("libxml2 " + ".".join(str(n) for n in etree.LIBXML_VERSION), run_lxml))
        except ImportError:
            print("lxml is not installed (pip install lxml): Expat only")
        for name, run in references:
            wrong = [(t, want) for t, want in counted if run(t) != want]
            print("%s: %d of the %d counted tests as the suite says" % (name, len(counted) - len(wrong), len(counted)))
            for t, want in wrong[:args.show]:
                print("    %-28s should %s: %s" % (t["ID"], want, t["description"][:110]))
        return 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda t: run_litebird(args.binary, t, args.timeout), tests))

    by_catalog = {}
    failures = []
    uncounted = {}
    broken = []
    passed = total = canonical_checked = 0
    for test, (outcome, output, message) in zip(tests, results):
        if outcome in ("crash", "timeout"):
            broken.append((test, outcome, message))
        want, reason = classify(test)
        if want and outcome == "encoding":
            want, reason = None, "an encoding this build cannot read"
        if not want:
            uncounted[reason] = uncounted.get(reason, 0) + 1
            continue
        total += 1
        row = by_catalog.setdefault(test["catalog"], [0, 0])
        row[1] += 1
        problem = None
        if outcome != want:
            problem = "should %s, did %s" % (want, outcome)
        elif want == "accept" and test["output"]:
            canonical_checked += 1
            expected = open(test["output"], "rb").read()
            if output != expected:
                problem = "canonical form differs"
        if problem:
            failures.append((test, problem, message, output))
        else:
            passed += 1
            row[0] += 1

    if not args.quiet:
        print("Per-catalog results for Litebird (passed/counted):")
        for catalog in sorted(by_catalog):
            ok, count = by_catalog[catalog]
            print(" %s %-40s %4d/%-4d" % (" " if ok == count else "!", catalog, ok, count))
        print()
        if args.show:
            print("First %d failures:" % min(args.show, len(failures)))
            for test, problem, message, output in failures[:args.show]:
                print("  %s (%s): %s" % (test["ID"], os.path.relpath(test["path"], root), problem))
                print("      %s" % test["description"][:200])
                if message:
                    print("      litebird: %s" % message[:200])
                if problem == "canonical form differs":
                    print("      expected: %r" % open(test["output"], "rb").read()[:200])
                    print("      actual:   %r" % output[:200])
            print()
        print("Not counted: " + "; ".join("%d %s" % (n, r) for r, n in sorted(uncounted.items(), key=lambda x: -x[1])))
    for test, outcome, message in broken:
        print("%s: %s (%s) %s" % (outcome.upper(), test["ID"], os.path.relpath(test["path"], root), message))
    percent = 100.0 * passed / total if total else 0.0
    print("XML conformance (W3C suite): %d/%d (%.1f%%)  [%d canonical forms compared, %d documents run, %d crashes, %d timeouts]"
          % (passed, total, percent, canonical_checked, len(tests),
             sum(1 for b in broken if b[1] == "crash"), sum(1 for b in broken if b[1] == "timeout")))
    if args.json:
        with open(args.json, "w") as out:
            json.dump({"passed": passed, "total": total, "run": len(tests), "not_counted": uncounted,
                       "failures": [t["ID"] for t, _, _, _ in failures]}, out, indent=1)
    if broken:
        return 1
    if passed < args.min_pass:
        print("error: %d passed, the floor is %d" % (passed, args.min_pass))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
