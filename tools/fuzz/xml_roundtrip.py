#!/usr/bin/env python3
"""Trees that scripts build, written as XML and read back: is it the same tree?

An XML serializer has one job that can be checked without asking anybody what
the text should look like: the text, parsed, must be the tree it was written
from. The same elements in the same namespaces, the same attributes in the
same namespaces with the same values, the same text.

That is where serializers go wrong, and where the standard's own algorithm
does (w3c/DOM-Parsing issues 44, 45, 75): a script can give an element the
prefix `p` for one namespace inside an element that declares `p` as another,
put a declaration on an element that contradicts the element itself, or ask
for a prefix the serializer was about to invent. This makes such trees:

    createElementNS / setAttributeNS with namespaces and prefixes drawn from
    small pools, so that they collide; xmlns and xmlns:p attributes that say
    something else than the tree does; an `xmlns` attribute in no namespace;
    xml:lang; text and attribute values with every character that needs
    escaping; comments, CDATA sections, processing instructions.

Each tree is serialized by Litebird (XMLSerializer) and then read twice:

    Litebird   DOMParser, in the same page
    Expat      Python's pyexpat, which shares no code with Litebird

and each reading is compared with the tree as it was built: namespace and
local name of every element and attribute, values, text, node by node.
Prefixes and namespace declarations are not compared: a serializer may
rename and redeclare, that is its work.

Then the tree that was read back is serialized and read again: it must still
be the same tree, and from there on the text must not change any more. (The
first text and the second may differ in prefixes: a declaration the
serializer had to add the first time is an attribute of the element the
second time, and the standard lets an element take a prefix declared on it.)

What is left out on purpose, because no text can carry it and the standard
does not ask XMLSerializer to refuse: a carriage return in text (written as
it is, read as a line feed), `xmlns:p=""`, a comment holding `--`, a CDATA
section holding `]]>`. Unit tests state what happens to each.

Exit status 1 when any tree comes back different, or when Litebird crashes.

Usage:
    python3 tools/fuzz/xml_roundtrip.py [--count 3000] [--seed 20261114] [--show 5]
                                        [--binary build/litebird]
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile
import xml.parsers.expat as expat

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

DRIVER = r"""
(function () {
  var seed = __SEED__, count = __COUNT__;
  function rnd() { seed = seed * 48271 % 2147483647; return (seed - 1) / 2147483646; }
  function pick(list) { return list[Math.floor(rnd() * list.length)]; }

  var XMLNS = 'http://www.w3.org/2000/xmlns/', XML = 'http://www.w3.org/XML/1998/namespace';
  var XHTML = 'http://www.w3.org/1999/xhtml', SVG = 'http://www.w3.org/2000/svg';
  var namespaces = [null, null, 'urn:a', 'urn:a', 'urn:b', 'urn:c', XHTML, SVG];
  var prefixes = ['', '', 'p', 'p', 'q', 'r', 'ns1', 'ns2', 'ns3'];
  var locals = ['a', 'b', 'c', 'd', 'br', 'p', 'title', 'svg'];
  var values = ['', 'v', 'a<b', 'a&b', 'a>b', 'say "hi"', "it's", ' two  spaces ', 'tab\there',
                'line\nbreak', 'cr\rhere', ']]>', '\u00e9\u2603', '&amp;', 'urn:a'];
  var texts = ['x', 'a<b', 'a&b', 'a>b', 'say "hi"', "it's", ' two  spaces ', 'tab\there',
               'line\nbreak', ']]>', '\u00e9\u2603', '&amp;', '<!--', ' '];
  var doc = document.implementation.createDocument(null, null);

  function addAttribute(el) {
    var kind = rnd();
    try {
      if (kind < 0.22) {
        el.setAttributeNS(XMLNS, 'xmlns:' + pick(['p', 'q', 'r', 'ns1', 'ns2']), pick(['urn:a', 'urn:b', 'urn:c', 'urn:z']));
      } else if (kind < 0.34) {
        el.setAttributeNS(XMLNS, 'xmlns', pick(['', 'urn:a', 'urn:b', XHTML]));
      } else if (kind < 0.38) {
        el.setAttribute('xmlns', pick(['urn:a', 'urn:q', '']));
      } else if (kind < 0.43) {
        el.setAttributeNS(XML, 'xml:lang', pick(['en', 'fr']));
      } else {
        var ns = pick(namespaces), prefix = pick(prefixes), local = pick(['x', 'y', 'z', 'href']);
        el.setAttributeNS(ns, ns && prefix ? prefix + ':' + local : local, pick(values));
      }
    } catch (e) {}
  }

  function makeElement(depth) {
    var ns = pick(namespaces), prefix = pick(prefixes), local = pick(locals);
    var el = doc.createElementNS(ns, ns && prefix ? prefix + ':' + local : local);
    var attributes = Math.floor(rnd() * 5);
    for (var i = 0; i < attributes; i++) addAttribute(el);
    var children = depth < 4 ? Math.floor(rnd() * 4) : 0;
    var text = false;
    for (var j = 0; j < children; j++) {
      var kind = rnd();
      if (kind < 0.55) { el.appendChild(makeElement(depth + 1)); text = false; }
      else if (kind < 0.75) { if (!text) el.appendChild(doc.createTextNode(pick(texts))); text = true; }
      else if (kind < 0.85) { el.appendChild(doc.createCDATASection(pick(['', 'raw <b> & text', ']] >', 'x']))); text = false; }
      else if (kind < 0.93) { el.appendChild(doc.createComment(pick(['', 'note', ' a - b ', '<x>']))); text = false; }
      else { el.appendChild(doc.createProcessingInstruction(pick(['t', 'xml-stylesheet']), pick(['', 'a b', 'href="x"']))); text = false; }
    }
    return el;
  }

  function dump(node) {
    if (node.nodeType === 1) {
      var attrs = [];
      for (var i = 0; i < node.attributes.length; i++) {
        var a = node.attributes[i];
        if (a.namespaceURI === XMLNS) continue;
        if (a.namespaceURI === null && a.name === 'xmlns') continue;
        attrs.push([a.namespaceURI || '', a.localName, a.value]);
      }
      var kids = [];
      for (var c = node.firstChild; c; c = c.nextSibling) {
        var d = dump(c);
        if (d[0] === 'T' && kids.length && kids[kids.length - 1][0] === 'T') kids[kids.length - 1][1] += d[1];
        else kids.push(d);
      }
      return ['E', node.namespaceURI || '', node.localName, attrs, kids];
    }
    if (node.nodeType === 3) return ['T', node.data];
    if (node.nodeType === 4) return ['C', node.data];
    if (node.nodeType === 8) return ['#', node.data];
    if (node.nodeType === 7) return ['?', node.target, node.data];
    return ['unknown', node.nodeType];
  }

  // How many names in the tree contradict a declaration in scope: an element
  // or attribute with prefix p in namespace A where the nearest xmlns:p says
  // B, an element with no prefix where the nearest xmlns says another
  // namespace than its own. This is what the generator is for; the count is
  // printed so that a run that made none cannot pass for a test.
  function declared(node, prefix) {
    for (var e = node; e && e.nodeType === 1; e = e.parentNode) {
      var value = e.getAttributeNS(XMLNS, prefix === '' ? 'xmlns' : prefix);
      if (value !== null && e.hasAttributeNS(XMLNS, prefix === '' ? 'xmlns' : prefix)) return value;
    }
    return null;
  }
  function contradictions(node) {
    var found = 0;
    if (node.nodeType !== 1) return 0;
    var says = declared(node, node.prefix || '');
    if (says !== null && says !== (node.namespaceURI || '')) found++;
    for (var i = 0; i < node.attributes.length; i++) {
      var a = node.attributes[i];
      if (!a.prefix || a.namespaceURI === XMLNS || a.namespaceURI === XML) continue;
      says = declared(node, a.prefix);
      if (says !== null && says !== a.namespaceURI) found++;
    }
    for (var c = node.firstChild; c; c = c.nextSibling) found += contradictions(c);
    return found;
  }

  var serializer = new XMLSerializer(), parser = new DOMParser();
  for (var n = 0; n < count; n++) {
    var record = { n: n };
    try {
      var root = makeElement(0);
      record.tree = dump(root);
      record.contradictions = contradictions(root);
      record.text = serializer.serializeToString(root);
      var back = parser.parseFromString(record.text, 'application/xml');
      record.back = dump(back.documentElement);
      record.again = serializer.serializeToString(back.documentElement);
      var second = parser.parseFromString(record.again, 'application/xml');
      record.back2 = dump(second.documentElement);
      record.again2 = serializer.serializeToString(second.documentElement);
    } catch (e) {
      record.error = String(e);
    }
    console.log('RT ' + JSON.stringify(record));
  }
})();
"""

SEPARATOR = "\x01"


def normal(node):
    """A dump with its attributes in one order and neighbouring text joined."""
    if node[0] != "E":
        return node
    children = []
    for child in node[4]:
        child = normal(child)
        if child[0] == "T" and children and children[-1][0] == "T":
            children[-1] = ["T", children[-1][1] + child[1]]
        elif child[0] == "T" and child[1] == "":
            continue
        else:
            children.append(child)
    return ["E", node[1], node[2], sorted(node[3]), children]


def split(name):
    return name.split(SEPARATOR, 1) if SEPARATOR in name else ["", name]


def expat_dump(text):
    """What Expat reads in `text`, as the same kind of dump; None if it refuses."""
    parser = expat.ParserCreate(namespace_separator=SEPARATOR)
    top = ["E", "", "#document", [], []]
    stack = [top]
    cdata = [False]

    def start(name, attributes):
        namespace, local = split(name)
        element = ["E", namespace, local, [split(key) + [value] for key, value in attributes.items()], []]
        stack[-1][4].append(element)
        stack.append(element)

    def end(_name):
        stack.pop()

    def characters(data):
        children = stack[-1][4]
        if cdata[0]:
            children[-1][1] += data
        elif children and children[-1][0] == "T":
            children[-1][1] += data
        else:
            children.append(["T", data])

    def start_cdata():
        stack[-1][4].append(["C", ""])
        cdata[0] = True

    def end_cdata():
        cdata[0] = False
        # Text after a CDATA section is a node of its own.
        stack[-1][4].append(["T", ""])

    parser.StartElementHandler = start
    parser.EndElementHandler = end
    parser.CharacterDataHandler = characters
    parser.StartCdataSectionHandler = start_cdata
    parser.EndCdataSectionHandler = end_cdata
    parser.CommentHandler = lambda data: stack[-1][4].append(["#", data])
    parser.ProcessingInstructionHandler = lambda target, data: stack[-1][4].append(["?", target, data])
    try:
        parser.Parse(text.encode("utf-8"), True)
    except expat.ExpatError as error:
        return None, str(error)
    return (normal(top[4][0]) if top[4] else None), ""


def run_chunk(binary, seed, count, workdir, timeout):
    script = os.path.join(workdir, "driver-%d.js" % seed)
    page = os.path.join(workdir, "page.html")
    with open(script, "w", encoding="utf-8") as out:
        out.write(DRIVER.replace("__SEED__", str(seed)).replace("__COUNT__", str(count)))
    with open(page, "w", encoding="utf-8") as out:
        out.write("<!DOCTYPE html><title>round trip</title><body></body>")
    try:
        done = subprocess.run([binary, "--run-script", script, "--url=https://roundtrip.test/", page],
                              capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "ran out of time (%ds)" % timeout
    if done.returncode != 0:
        return None, "exit status %d: %s" % (done.returncode, done.stderr.decode("utf-8", "replace")[-300:])
    records = []
    for line in done.stdout.decode("utf-8", "replace").split("\n"):
        if line.startswith("RT "):
            records.append(json.loads(line[3:]))
    if len(records) != count:
        return None, "%d of %d trees reported: %s" % (len(records), count, done.stdout.decode("utf-8", "replace")[-300:])
    return records, ""


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=os.path.join(REPO, "build", "litebird"))
    parser.add_argument("--count", type=int, default=3000)
    parser.add_argument("--seed", type=int, default=20261114)
    parser.add_argument("--show", type=int, default=5)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if not os.path.exists(args.binary):
        sys.stderr.write("%s: no such file (run `make` first)\n" % args.binary)
        return 2

    chunk = 250
    failures = []
    crashed = 0
    total = 0
    seen = {"trees": 0, "names": 0, "second text differs": 0}
    with tempfile.TemporaryDirectory(prefix="litebird-roundtrip-") as workdir:
        for start in range(0, args.count, chunk):
            size = min(chunk, args.count - start)
            records, problem = run_chunk(args.binary, args.seed + start, size, workdir, args.timeout)
            if records is None:
                crashed += 1
                failures.append(("seed %d" % (args.seed + start), "Litebird: " + problem, ""))
                continue
            for record in records:
                total += 1
                label = "seed %d, tree %d" % (args.seed + start, record["n"])
                if "error" in record:
                    failures.append((label, "a script error: " + record["error"], record.get("text", "")))
                    continue
                tree = normal(record["tree"])
                text = record["text"]
                if normal(record["back"]) != tree:
                    failures.append((label, "Litebird reads another tree back", text))
                    continue
                read, problem = expat_dump(text)
                if read is None:
                    failures.append((label, "Expat refuses it: " + problem, text))
                    continue
                if read != tree:
                    failures.append((label, "Expat reads another tree", text))
                    continue
                if normal(record["back2"]) != tree:
                    failures.append((label, "read back and written again, it is another tree: " + record["again"][:300], text))
                    continue
                if record["again2"] != record["again"]:
                    failures.append((label, "the text keeps changing: %s  then  %s"
                                     % (record["again"][:200], record["again2"][:200]), text))
                    continue
                if record["contradictions"]:
                    seen["trees"] += 1
                    seen["names"] += record["contradictions"]
                if record["again"] != text:
                    seen["second text differs"] += 1

    print("XML round trip: %d trees, %d come back the same from Litebird and from Expat, %d do not"
          % (total, total - len(failures) + crashed, len(failures) - crashed))
    print("  %d of the trees that pass hold a name that contradicts a declaration in scope (%d such names);"
          % (seen["trees"], seen["names"]))
    print("  %d were written with other prefixes the second time, and none changed after that"
          % seen["second text differs"])
    for label, what, text in failures[:args.show]:
        print("  %s: %s" % (label, what))
        if text:
            print("      " + text[:400])
    if crashed:
        print("  %d run(s) of Litebird did not finish" % crashed)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
