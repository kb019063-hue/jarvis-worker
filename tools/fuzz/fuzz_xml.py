#!/usr/bin/env python3
"""Mutation fuzzing for the XML parser and what stands on it.

tools/fuzz/diff_xml.py builds documents from a grammar and damages them in
one place; it knows what the answer should be, and so it only makes documents
it understands. This script understands nothing. It takes real documents (the
W3C suite when `make xml-conformance` has downloaded it, and a dozen written
below), changes a few bytes, cuts them short, pastes in the tokens XML is made
of, and gives each result to Litebird two ways:

  bytes   `litebird --xml-events FILE`: the encoding detection, the parser,
          every event.
  dom     a page that asks for the same bytes as a `data:` URL: the base64
          and MIME code, `responseText`, `responseXML` (the tree builder),
          `XMLSerializer`, `DOMParser` on what was written, `innerHTML` read
          and assigned back, a clone and an import.

Then the documents of the suite that are meant to be well formed go to the
page as they are, for the last of the checks below.

A finding is

  * a signal, a timeout or a sanitizer report, either way;
  * an exit status of `--xml-events` that is none of 0 (well formed), 1 (not)
    and 3 (an encoding this build cannot read);
  * the two ways disagreeing on whether the bytes are a document;
  * a document that is not the same tree after it is written and read again,
    or whose `innerHTML`, once it has been assigned to itself, changes when
    that is done again. (A carriage return in text is the one thing that
    does not survive being written: XML reads it as a line feed. Such
    documents are not compared.)

Run against a sanitizer build for real value; `make asan` does.

    python3 tools/fuzz/fuzz_xml.py [--iterations N] [--binary PATH] [--seed S]
"""

from __future__ import annotations

import argparse
import base64
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
DEFAULT_BINARY = "build-asan/litebird"
SUITE = REPO / "build" / "xml-conformance" / "xmlconf"
PAGE_URL = "https://example.test/fuzz.html"
SEED_LIMIT = 16 * 1024      # larger documents of the suite are not used as seeds
MUTANT_LIMIT = 64 * 1024

BUILT_IN = [
    b'<a/>',
    b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n<r a="1" b=\'2\'><c>t &amp; u &#65;&#x42;</c><!-- c --><?p d?><![CDATA[<x>]]></r>\n',
    b'<!DOCTYPE r [<!ENTITY e "v"><!ENTITY f "&e;&e;"><!ATTLIST r a CDATA "d" b NMTOKENS " x  y "><!ELEMENT r (#PCDATA|c)*>]><r b=" p  q ">&f;<c/></r>',
    b'<r xmlns="urn:d" xmlns:p="urn:p" p:a="1"><p:c xmlns:p="urn:q" p:a="2"/><c xmlns=""/></r>',
    b'<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN" "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd">'
    b'<html xmlns="http://www.w3.org/1999/xhtml"><head><title>t</title></head><body><p>a&nbsp;b&eacute;</p></body></html>',
    b'<!DOCTYPE r [<!ENTITY % p "<!ENTITY q \'v\'>">%p;<!NOTATION n SYSTEM "s"><!ENTITY u SYSTEM "x" NDATA n>]><r>&q;</r>',
    b'<!DOCTYPE r SYSTEM "r.dtd" [<!ATTLIST r xmlns CDATA #FIXED "urn:f" xml:space (default|preserve) "preserve">]><r>&undeclared;</r>',
    b'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" viewBox="0 0 1 1"><use xlink:href="#a"/><text xml:lang="en">t</text></svg>',
    b'\xef\xbb\xbf<?xml version="1.0"?><r>\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80</r>',
    b'<?xml version="1.0" encoding="ISO-8859-1"?><r a="\xe9">\xe9</r>',
    '\ufeff<?xml version="1.0" encoding="UTF-16"?><r a="\u00e9">\u20ac</r>'.encode("utf-16-le"),
    '<?xml version="1.0" encoding="UTF-16BE"?><r>\u00e9</r>'.encode("utf-16-be"),
    b'<r>\r\n a\rb\n<![CDATA[]]]]><![CDATA[>]]>&#13;&#10;&#9;<e a="&#13;&#10;&#9; \t\n"/></r>',
]

INTERESTING = [b"<", b">", b"</", b"/>", b"<!", b"<?", b"?>", b"<![CDATA[", b"]]>", b"&", b"&#", b"&#x", b";", b"%",
               b"--", b"<!--", b"-->", b'"', b"'", b"=", b"\x00", b"\xff", b"\xfe\xff", b"\xff\xfe", b"\xef\xbb\xbf",
               b"\r", b"\r\n", b"\xc2\x85", b"\xe2\x80\xa8", b"\xed\xa0\x80", b"\xef\xbf\xbe", b":", b"xmlns", b"xmlns:",
               b' xmlns="u"', b' xmlns:p="u"', b' xmlns:p=""', b" xml:space='preserve'", b"p:", b"xml:", b"xmlns:xml",
               b"<!DOCTYPE r [", b"]>", b"<!ENTITY e 'v'>", b"<!ENTITY % p 'v'>", b"%p;", b"&e;", b"&amp;", b"&lt;",
               b"<!ATTLIST r a CDATA 'v'>", b"<!ELEMENT r ANY>", b"<!NOTATION n SYSTEM 's'>", b" SYSTEM 's'",
               b" PUBLIC 'p' 's'", b" NDATA n", b"#IMPLIED", b"#FIXED", b"(", b")", b"|", b"*",
               b'<?xml version="1.0"?>', b'<?xml version="1.1"?>', b' encoding="UTF-16"', b' encoding="ISO-8859-1"',
               b' encoding="EUC-JP"', b' standalone="yes"', b"<r>", b"</r>", b"<a>", b"</a>"]

SANITIZER_MARKS = (b"AddressSanitizer", b"UndefinedBehaviorSanitizer", b"runtime error:", b"SUMMARY: ", b"LeakSanitizer")

SCRIPT = r"""
var S = new XMLSerializer(), P = new DOMParser(), out = [];
// A tree as text: every node, and every element and attribute by its
// namespace and local name. Namespace declarations are left out: the
// serializer may drop one that says what is so already (xmlns:xml, a prefix
// an ancestor declared the same way) and add one that is needed, and prefixes
// may differ. No recursion: a mutant can be deep.
function shape(doc) {
    var text = [], stack = [doc];
    while (stack.length) {
        var n = stack.pop();
        if (typeof n === 'string') { text.push(n); continue; }
        if (n.nodeType === 1) {
            var attrs = [];
            for (var k = 0; k < n.attributes.length; k++) {
                var a = n.attributes[k];
                if (a.namespaceURI !== 'http://www.w3.org/2000/xmlns/') attrs.push('{' + a.namespaceURI + '}' + a.localName + '=' + JSON.stringify(a.value));
            }
            text.push('<{' + n.namespaceURI + '}' + n.localName + ' ' + attrs.sort().join(' ') + '>');
            stack.push('</>');
        } else if (n.nodeType === 10) {
            text.push('<!DOCTYPE ' + n.name + ' [' + n.publicId + '] [' + n.systemId + ']>');
        } else if (n.nodeType !== 9) {
            text.push(n.nodeType + ':' + (n.nodeType === 7 ? n.target + ' ' : '') + JSON.stringify(n.data));
        }
        for (var c = n.lastChild; c; c = c.previousSibling) stack.push(c);
    }
    return text.join('');
}
for (var i = 0; i < docs.length; i++) {
    var x = new XMLHttpRequest();
    x.open('GET', 'data:application/xml;base64,' + docs[i], false);
    x.send();
    var text = x.responseText, d = x.responseXML, verdict = 'none';
    if (d) {
        verdict = 'same';
        var written = S.serializeToString(d);
        var again = P.parseFromString(written, 'application/xml');
        if (written.indexOf('\r') < 0 && shape(again) !== shape(d)) verdict = 'CHANGED: ' + written.slice(0, 200);
        // innerHTML read, assigned, read and assigned again: the second
        // reading must be what comes back. (Not the first: the standard
        // writes each child without its surroundings, so a child in no
        // namespace is written "<c/>" and lands in the default namespace
        // of the element it is assigned to. jsdom does the same.)
        var root = d.documentElement, inner = root.innerHTML;
        if (inner.indexOf('\r') < 0) {
            root.innerHTML = inner;
            var once = root.innerHTML;
            root.innerHTML = once;
            if (root.innerHTML !== once) verdict = 'INNER: ' + once.slice(0, 200);
        }
        document.importNode(root, true);
        d.cloneNode(true);
    }
    // The text as a script holds it, through DOMParser. Nothing is asked
    // of the answer: a string has lost what the bytes said of an encoding.
    P.parseFromString(text, 'application/xml');
    P.parseFromString(text, 'image/svg+xml');
    out.push(i + ' ' + verdict);
}
console.log(out.join('\n'));
"""


def seeds() -> tuple[list[bytes], list[bytes], int]:
    """(documents that are meant to be well formed, the others, how many came
    from the suite). The suite says which is which by where a file lies."""
    good = list(BUILT_IN)
    bad: list[bytes] = []
    from_suite = 0
    if SUITE.is_dir():
        for path in sorted(SUITE.rglob("*.xml")):
            try:
                data = path.read_bytes()
            except OSError:
                continue
            if 0 < len(data) <= SEED_LIMIT:
                (bad if "not-wf" in path.as_posix() else good).append(data)
                from_suite += 1
    return good, bad, from_suite


def mutate(data: bytes, other: bytes, rng: random.Random) -> bytes:
    buf = bytearray(data)
    # Half of the time one change only: a document that is still a document
    # goes on through the tree builder and the serializer, and most do not
    # survive six changes.
    for _ in range(1 if rng.random() < 0.5 else rng.randint(2, 6)):
        choice = rng.random()
        if not buf:
            buf.extend(rng.choice(INTERESTING))
            continue
        if choice < 0.25:
            buf[rng.randrange(len(buf))] = rng.randrange(256)
        elif choice < 0.55:
            pos = rng.randrange(len(buf) + 1)
            buf[pos:pos] = rng.choice(INTERESTING)
        elif choice < 0.68:
            pos = rng.randrange(len(buf))
            del buf[pos:pos + rng.randint(1, 16)]
        elif choice < 0.80:
            start = rng.randrange(len(buf))
            end = min(len(buf), start + rng.randint(1, 64))
            pos = rng.randrange(len(buf) + 1)
            buf[pos:pos] = buf[start:end]
        elif choice < 0.90:
            # Cut short: the end of the input in the middle of anything.
            del buf[rng.randrange(len(buf)):]
        elif choice < 0.96 and other:
            # A piece of another document, declarations and all.
            start = rng.randrange(len(other))
            pos = rng.randrange(len(buf) + 1)
            buf[pos:pos] = other[start:start + rng.randint(1, 256)]
        else:
            buf = bytearray(bytes(buf) * 2)
        if len(buf) > MUTANT_LIMIT:
            del buf[MUTANT_LIMIT:]
    return bytes(buf)


def run(command: list[str], timeout: float):
    try:
        return subprocess.run(command, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None


def fault(completed, statuses: tuple[int, ...]) -> str | None:
    """Why a run is a finding, or None."""
    if completed is None:
        return "timeout"
    if any(mark in completed.stderr for mark in SANITIZER_MARKS):
        lines = [line for line in completed.stderr.decode("utf-8", "replace").splitlines() if "ERROR" in line or "runtime error" in line]
        return "sanitizer: " + (lines[0][:200] if lines else "report")
    if completed.returncode not in statuses:
        return f"exit {completed.returncode}"
    return None


def dom_verdicts(binary: str, mutants: list[bytes], scratch: str, timeout: float):
    """What the page says of each mutant: 'none', 'same', or what went wrong.
    A string in place of the list when the process itself failed."""
    script = os.path.join(scratch, "fuzz.js")
    page = os.path.join(scratch, "fuzz.html")
    with open(script, "w", encoding="ascii") as out:
        out.write("var docs = [" + ",".join('"' + base64.b64encode(m).decode("ascii") + '"' for m in mutants) + "];\n" + SCRIPT)
    with open(page, "w", encoding="ascii") as out:
        out.write("<!DOCTYPE html><title>fuzz</title>")
    completed = run([binary, "--run-script", script, "--url=" + PAGE_URL, page], timeout)
    reason = fault(completed, (0,))
    if reason:
        return reason
    verdicts = {}
    for line in completed.stdout.decode("utf-8", "replace").splitlines():
        index, _, verdict = line.partition(" ")
        if index.isdigit():
            verdicts[int(index)] = verdict
    if len(verdicts) != len(mutants):
        return "the page reported %d of %d documents" % (len(verdicts), len(mutants))
    return [verdicts[i] for i in range(len(mutants))]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--iterations", type=int, default=600)
    parser.add_argument("--seed", type=int, default=20261114)
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds for one process")
    parser.add_argument("--batch", type=int, default=25, help="documents a page is given at once")
    parser.add_argument("--keep", default=None, help="directory for the documents that are findings (default: next to the binary)")
    parser.add_argument("--no-whole", dest="whole", action="store_false",
                        help="do not also run the documents of the suite as they are")
    args = parser.parse_args()

    if not Path(args.binary).exists():
        print(f"error: {args.binary} not found (run `make asan`)", file=sys.stderr)
        return 2
    keep = Path(args.keep) if args.keep else Path(args.binary).resolve().parent
    good, bad, from_suite = seeds()
    everything = good + bad
    rng = random.Random(args.seed)
    mutants = []
    for _ in range(args.iterations):
        # A third start from the documents written above, which between
        # them use every construct; of the rest, two in three from a document
        # of the suite that was well formed.
        pick = rng.random()
        source = BUILT_IN if pick < 0.34 else good if pick < 0.78 or not bad else bad
        mutants.append(mutate(rng.choice(source), rng.choice(everything), rng))

    findings: list[tuple[int, str]] = []
    accepted = [False] * len(mutants)
    with tempfile.TemporaryDirectory() as scratch:
        # 1. The bytes, one process each.
        path = os.path.join(scratch, "mutant.xml")
        for index, data in enumerate(mutants):
            with open(path, "wb") as out:
                out.write(data)
            completed = run([args.binary, "--xml-events", path], args.timeout)
            reason = fault(completed, (0, 1, 3))
            if reason:
                findings.append((index, "bytes: " + reason))
            else:
                accepted[index] = completed.returncode == 0

        # 2. The same through a page, a batch at a time; a batch that fails
        #    is run again one document at a time to find which.
        for start in range(0, len(mutants), args.batch):
            batch = list(range(start, min(start + args.batch, len(mutants))))
            verdicts = dom_verdicts(args.binary, [mutants[i] for i in batch], scratch, args.timeout)
            groups = [(batch, verdicts)]
            if isinstance(verdicts, str):
                groups = [([i], dom_verdicts(args.binary, [mutants[i]], scratch, args.timeout)) for i in batch]
            for indexes, result in groups:
                if isinstance(result, str):
                    findings.extend((i, "dom: " + result) for i in indexes)
                    continue
                for i, verdict in zip(indexes, result):
                    if verdict not in ("none", "same"):
                        findings.append((i, "dom: " + verdict))
                    elif (verdict != "none") != accepted[i] and not any(index == i for index, _ in findings):
                        findings.append((i, "the parser says %s and the page says %s" %
                                         ("a document" if accepted[i] else "no document", "a document" if verdict != "none" else "none")))

        # 3. The documents of the suite as they are, through the page only
        #    (`make xml-conformance` has given each to the parser): every
        #    one that is a document must be the same tree after it is
        #    written and read.
        whole = good if args.whole else []
        whole_documents = 0
        for start in range(0, len(whole), args.batch):
            batch = list(range(start, min(start + args.batch, len(whole))))
            verdicts = dom_verdicts(args.binary, [whole[i] for i in batch], scratch, args.timeout)
            groups = [(batch, verdicts)]
            if isinstance(verdicts, str):
                groups = [([i], dom_verdicts(args.binary, [whole[i]], scratch, args.timeout)) for i in batch]
            for indexes, result in groups:
                for position, i in enumerate(indexes):
                    verdict = result if isinstance(result, str) else result[position]
                    whole_documents += verdict == "same"
                    if verdict not in ("none", "same"):
                        mutants.append(whole[i])
                        findings.append((len(mutants) - 1, "dom, a document as it is: " + verdict))

    keep.mkdir(parents=True, exist_ok=True)
    for index, reason in sorted(findings):
        artifact = keep / f"xml-finding-{index:04d}.xml"
        artifact.write_bytes(mutants[index])
        print(f"  ! {reason}\n    saved to {artifact}")
    if whole:
        print("fuzz xml: %d documents as they are, %d of them documents to this parser: each the same tree after it is written and read"
              % (len(whole), whole_documents) if not any("as it is" in reason for _, reason in findings) else
              "fuzz xml: %d documents as they are, %d of them documents to this parser" % (len(whole), whole_documents))
    print("fuzz xml: %d mutations of %d documents (%s), %d of them well formed, %d finding(s)" %
          (args.iterations, len(everything),
           "%d of the W3C suite" % from_suite if from_suite else "the W3C suite is not downloaded: the %d written in the script" % len(BUILT_IN),
           sum(accepted[:args.iterations]), len(findings)))
    return 1 if findings else 0


if __name__ == "__main__":
    raise SystemExit(main())
