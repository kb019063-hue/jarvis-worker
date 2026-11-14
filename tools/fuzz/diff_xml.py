#!/usr/bin/env python3
"""Random XML documents through Litebird, Expat and libxml2, results compared.

The W3C suite is 2,585 documents somebody thought of. This makes documents
nobody wrote: a grammar puts together declarations, entities that refer to
entities, attribute defaults, namespaces, CDATA sections, references and
comments, and half of the documents are then damaged (characters deleted,
inserted, swapped, pieces copied, the end cut off). Each document goes
through three parsers:

    Litebird   build/litebird --xml-events
    Expat      the parser Firefox uses (Python's pyexpat)
    libxml2    the parser Chrome and Safari use (lxml, when installed)

and each either rejects it or prints what it read: one line for the DOCTYPE
and for each element, attribute, run of text, comment and processing
instruction, names written with their namespaces.

    agree       all three say the same
    DIFFER      Expat and libxml2 say the same and Litebird says another thing
    references  Expat and libxml2 do not agree with each other. That is not
    disagree    counted against Litebird, and it is not rare: Expat keeps the
                name characters of the editions before the fifth, libxml2
                treats some validity errors as fatal, and the two differ on
                an entity that is not declared when part of the DTD was not
                read.

One kind of document is judged by the grammar and not by the two parsers: an
XML declaration whose version is not "1." and at least one digit (production
26, VersionNum). Expat does not look at the version at all (version="abc"
passes) and libxml2 lets "1." through with no digit, so on version="1." the
two agree with each other and not with XML. Litebird must refuse every such
document; the summary says how many there were.

Exit status 1 when more than --max-differ documents DIFFER, or when Litebird
crashes or runs out of time on any. Exit status 2 when lxml is not installed
(pip install lxml): Expat alone keeps the name characters of the old
editions, so it cannot be the only reference. --expat-only runs without
libxml2 all the same and then fails on crashes and timeouts only.

Usage:
    python3 tools/fuzz/diff_xml.py [--count 2000] [--seed 20261114] [--show 5]
                                   [--binary build/litebird] [--max-differ 0]
                                   [--expat-only]
"""
import argparse
import concurrent.futures
import json
import os
import random
import re
import subprocess
import sys
import tempfile
import xml.parsers.expat as expat

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

try:
    from lxml import etree
except ImportError:
    etree = None


def quoted(text):
    return json.dumps(text, ensure_ascii=False)


def doctype_first(lines):
    """The dump with its DOCTYPE line moved to the front: libxml2's tree does
    not say where the DOCTYPE stood among the comments of the prolog."""
    return "\n".join(sorted(lines, key=lambda line: not line.startswith("doctype ")))


# --- the references ----------------------------------------------------------------

# Expat joins a namespace and a local name with one character, and refuses a
# namespace that holds that character. This one cannot occur in XML at all.
SEPARATOR = "\x01"

def expat_events(data):
    out, text, state = [], [], {"dtd": False}

    def flush():
        if text:
            out.append("text " + quoted("".join(text)))
            text.clear()

    def expanded(name):
        if SEPARATOR in name:
            uri, local = name.split(SEPARATOR, 1)
            return "{%s}%s" % (uri, local)
        return name

    def start(name, attributes):
        flush()
        out.append("element " + expanded(name))
        for key, value in sorted((expanded(k), v) for k, v in attributes.items()):
            out.append("attr %s=%s" % (key, quoted(value)))

    def end(name):
        flush()
        out.append("end")

    def comment(data):
        if not state["dtd"]:
            flush()
            out.append("comment " + quoted(data))

    def instruction(target, data):
        if not state["dtd"]:
            flush()
            out.append("pi %s %s" % (target, quoted(data)))

    def doctype(name, system_id, public_id, has_internal_subset):
        state["dtd"] = True
        out.append("doctype %s|%s|%s" % (name, public_id or "", system_id or ""))

    def end_doctype():
        state["dtd"] = False

    parser = expat.ParserCreate(namespace_separator=SEPARATOR)
    parser.SetParamEntityParsing(expat.XML_PARAM_ENTITY_PARSING_ALWAYS)
    parser.ExternalEntityRefHandler = lambda *a: 1
    parser.StartElementHandler = start
    parser.EndElementHandler = end
    parser.CharacterDataHandler = text.append
    parser.CommentHandler = comment
    parser.ProcessingInstructionHandler = instruction
    parser.StartDoctypeDeclHandler = doctype
    parser.EndDoctypeDeclHandler = end_doctype
    try:
        parser.Parse(data, True)
    except (expat.ExpatError, ValueError, LookupError) as error:
        return None, str(error)
    flush()
    return doctype_first(out), ""


def lxml_events(data):
    # attribute_defaults: libxml2's own tree builder leaves out the attributes
    # the DTD supplies unless asked; a browser, which builds its own tree
    # from libxml2's events, gets them.
    options = etree.XMLParser(resolve_entities=True, load_dtd=False, no_network=True, huge_tree=True,
                              attribute_defaults=True)
    try:
        root = etree.fromstring(data, options)
    except etree.XMLSyntaxError as error:
        return None, str(error)
    out, text = [], []

    def flush():
        if text:
            out.append("text " + quoted("".join(text)))
            text.clear()

    def add_text(value):
        if value:
            text.append(value)

    def other(node):
        flush()
        if node.tag is etree.Comment:
            out.append("comment " + quoted(node.text or ""))
        elif node.tag is etree.ProcessingInstruction:
            out.append("pi %s %s" % (node.target, quoted(node.text or "")))

    dtd = root.getroottree().docinfo.internalDTD
    if dtd is not None:
        out.append("doctype %s|%s|%s" % (dtd.name or "", dtd.external_id or "", dtd.system_url or ""))
    for node in reversed(list(root.itersiblings(preceding=True))):
        other(node)
    for event, node in etree.iterwalk(root, events=("start", "end", "comment", "pi")):
        if event == "start":
            flush()
            out.append("element " + node.tag)
            for key, value in sorted(node.attrib.items()):
                out.append("attr %s=%s" % (key, quoted(value)))
            add_text(node.text)
        elif event == "end":
            flush()
            out.append("end")
            if node is not root:
                add_text(node.tail)
        else:
            other(node)
            add_text(node.tail)
    for node in root.itersiblings():
        other(node)
    flush()
    return doctype_first(out), ""


def litebird_events(binary, path, timeout):
    try:
        done = subprocess.run([binary, "--xml-events", path], capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return "timeout", ""
    message = done.stderr.decode("utf-8", "replace").strip()
    if done.returncode == 0:
        return doctype_first(done.stdout.decode("utf-8", "replace").rstrip("\n").split("\n")), ""
    # 3: an encoding declaration that names nothing this build can read.
    if done.returncode in (1, 3):
        return None, message
    return "crash", "exit status %d %s" % (done.returncode, message[-300:])


# --- the generator ------------------------------------------------------------------

NAMES = ["a", "b", "c", "doc", "item", "x1", "long-name", "under_score", "dot.ted", "été", "日本", "αβ", "A", "B"]
PREFIXES = ["p", "q", "ns", "svg"]
URIS = ["urn:one", "urn:two", "http://www.w3.org/2000/svg", "http://example.test/ns"]
WORDS = ["text", "more text", " ", "\n  ", "\t", "ünïcödé", "日本語", "a > b", "x ]] y", "q'uote", 'd"quote', "100%", "a;b", "😀"]
TYPES = ["CDATA", "NMTOKEN", "NMTOKENS", "ID", "IDREF", "(one|two|three)"]


class Generator:
    def __init__(self, rng):
        self.rng = rng
        self.entities = []          # names of internal general entities, declared in this order
        self.attlists = {}

    def pick(self, items):
        return self.rng.choice(items)

    def chance(self, p):
        return self.rng.random() < p

    def char_ref(self):
        code = self.pick([65, 97, 38, 60, 62, 34, 39, 9, 10, 13, 32, 233, 0x3042, 0x1F600])
        return "&#%d;" % code if self.chance(0.5) else "&#x%X;" % code

    def reference(self, in_attribute):
        r = self.rng.random()
        if r < 0.35:
            return self.pick(["&lt;", "&gt;", "&amp;", "&quot;", "&apos;"])
        if r < 0.7 or not self.entities:
            return self.char_ref()
        name = self.pick(self.entities)
        # An entity with markup in it cannot go into an attribute value.
        if in_attribute and name.startswith("m"):
            return self.char_ref()
        return "&%s;" % name

    def text(self):
        parts = []
        for _ in range(self.rng.randint(1, 3)):
            parts.append(self.reference(False) if self.chance(0.3) else
                         self.pick(WORDS).replace("&", "&amp;").replace("<", "&lt;").replace("]]>", "]]&gt;"))
        return "".join(parts)

    def attribute_value(self):
        quote = self.pick(['"', "'"])
        parts = []
        for _ in range(self.rng.randint(0, 3)):
            if self.chance(0.3):
                parts.append(self.reference(True))
            else:
                word = self.pick(WORDS).replace("&", "&amp;").replace("<", "&lt;")
                parts.append(word.replace(quote, "&quot;" if quote == '"' else "&apos;"))
        return quote + "".join(parts) + quote

    def comment(self):
        return "<!--%s-->" % self.pick(["", " c ", "a - b", "multi\nline", "<not a tag>", "&amp;", "日本"])

    def instruction(self):
        return "<?%s%s?>" % (self.pick(["pi", "target", "xml-stylesheet", "x.y"]),
                             self.pick(["", " ", " data", "  two  words ", " a?b", " <>&"]))

    def element(self, depth, scope):
        scope = dict(scope)
        attributes = []
        if self.chance(0.25):
            prefix, uri = self.pick(PREFIXES), self.pick(URIS)
            scope[prefix] = uri
            attributes.append('xmlns:%s="%s"' % (prefix, uri))
        if self.chance(0.15):
            uri = self.pick(URIS + [""])
            attributes.append('xmlns="%s"' % uri)
        name = self.pick(NAMES)
        if scope and self.chance(0.3):
            name = self.pick(sorted(scope)) + ":" + name
        used = set()
        for _ in range(self.rng.randint(0, 3)):
            attribute = self.pick(NAMES)
            if scope and self.chance(0.2):
                attribute = self.pick(sorted(scope)) + ":" + attribute
            elif self.chance(0.05):
                attribute = self.pick(["xml:lang", "xml:space"])
            if attribute in used:
                continue
            used.add(attribute)
            attributes.append("%s%s=%s%s" % (attribute, self.pick(["", "", " "]), self.pick(["", "", "\n"]),
                                             self.attribute_value()))
        self.rng.shuffle(attributes)
        open_tag = "<" + name + "".join(self.pick([" ", "  ", "\n"]) + a for a in attributes)
        if depth <= 0 or self.chance(0.25):
            return open_tag + self.pick(["/>", " />", "></%s>" % name])
        parts = [open_tag + self.pick([">", " >"])]
        for _ in range(self.rng.randint(0, 4)):
            r = self.rng.random()
            if r < 0.4:
                parts.append(self.element(depth - 1, scope))
            elif r < 0.75:
                parts.append(self.text())
            elif r < 0.85:
                parts.append("<![CDATA[%s]]>" % self.pick(["", "raw <b> & text", "]] >", "x]]y", "\n"]))
            elif r < 0.93:
                parts.append(self.comment())
            else:
                parts.append(self.instruction())
        parts.append("</%s%s>" % (name, self.pick(["", "", " "])))
        return "".join(parts)

    def entity_value(self, with_markup):
        quote = self.pick(['"', "'"])
        parts = []
        for _ in range(self.rng.randint(0, 3)):
            r = self.rng.random()
            if r < 0.3 and self.entities:
                candidates = [e for e in self.entities if with_markup or not e.startswith("m")]
                if candidates:
                    parts.append("&%s;" % self.pick(candidates))
                    continue
            if r < 0.5:
                parts.append(self.pick(["&#38;#38;", "&#38;#60;", "&#62;", "&#x41;", "&#233;", "&#38;amp;"]))
            elif r < 0.6 and with_markup:
                parts.append(self.pick(["<i>in</i>", "<b/>", "<!--c-->", "<![CDATA[cd]]>", "<?p d?>"]))
            else:
                parts.append(self.pick(["value", "two words", " ", "ü", "日本"]))
        return quote + "".join(parts).replace(quote, "") + quote

    def doctype(self, root):
        r = self.rng.random()
        external = ""
        if r < 0.12:
            external = ' SYSTEM "x.dtd"'
        elif r < 0.2:
            external = ' PUBLIC "-//Test//DTD X//EN" "x.dtd"'
        declarations = []
        for _ in range(self.rng.randint(0, 6)):
            kind = self.rng.random()
            if kind < 0.45:
                markup = self.chance(0.3)
                name = ("m" if markup else "e") + str(len(self.entities))
                declarations.append("<!ENTITY %s %s>" % (name, self.entity_value(markup)))
                self.entities.append(name)
            elif kind < 0.7:
                element = self.pick(NAMES + [root])
                attribute = self.pick(NAMES)
                kind_of = self.pick(TYPES)
                if kind_of.startswith("("):
                    default = self.pick(["#IMPLIED", '"one"', '#FIXED "two"'])
                elif kind_of in ("ID",):
                    default = self.pick(["#IMPLIED", "#REQUIRED"])
                elif kind_of == "CDATA":
                    default = self.pick(["#IMPLIED", "#REQUIRED", '"a  default"', "'  spaced  '", '#FIXED "f"', '"&#65;&amp;"'])
                else:
                    default = self.pick(["#IMPLIED", '"tok"', '"  tok  "'])
                if (element, attribute) in self.attlists:
                    continue
                self.attlists[(element, attribute)] = kind_of
                declarations.append("<!ATTLIST %s %s %s %s>" % (element, attribute, kind_of, default))
            elif kind < 0.8:
                declarations.append(self.comment())
            elif kind < 0.86:
                declarations.append(self.instruction())
            elif kind < 0.93:
                declarations.append('<!NOTATION n%d %s>' % (len(declarations), self.pick(['SYSTEM "s"', 'PUBLIC "p"', 'PUBLIC "p" "s"'])))
            else:
                declarations.append('<!ENTITY ext%d SYSTEM "e.xml">' % len(declarations))
        subset = ""
        if declarations or self.chance(0.2):
            subset = " [" + self.pick(["", "\n"]) + self.pick(["", "\n", " "]).join(declarations) + self.pick(["", "\n"]) + "]"
        return "<!DOCTYPE %s%s%s>" % (root, external, subset)

    def document(self):
        parts = []
        if self.chance(0.4):
            parts.append('<?xml version="1.0"%s%s?>' % (self.pick(["", ' encoding="UTF-8"', " encoding='utf-8'"]),
                                                        self.pick(["", "", ' standalone="yes"', ' standalone="no"'])))
        parts.append(self.pick(["", "\n", " "]) if parts else "")
        if self.chance(0.2):
            parts.append(self.comment() + self.pick(["", "\n"]))
        if self.chance(0.1):
            parts.append(self.instruction())
        root = self.pick(NAMES)
        if self.chance(0.5):
            parts.append(self.doctype(root) + self.pick(["", "\n"]))
        body = self.element(self.rng.randint(0, 5), {})
        # The DOCTYPE names the root; give it the root it names most of the time.
        if self.chance(0.8):
            first = body[1:].split(" ")[0].split(">")[0].split("/")[0].split("\n")[0]
            if ":" not in first:
                body = body.replace("<" + first, "<" + root, 1)
                if body.endswith("</%s>" % first) or body.endswith("</%s >" % first):
                    body = body[:body.rfind("</")] + "</%s>" % root
        parts.append(body)
        if self.chance(0.15):
            parts.append(self.pick(["\n", ""]) + self.comment())
        if self.chance(0.1):
            parts.append(self.instruction())
        return "".join(parts)


NOISE = list("<>&;\"'/=![]?-%# \n\t:") + ["a", "é", "]]>", "<!--", "-->", "<![CDATA[", "&#", "&amp;", "<?", "?>", "xmlns", "\x00", "\x0b", "\ufffe"]


def damage(rng, text):
    for _ in range(rng.randint(1, 3)):
        if not text:
            break
        at = rng.randrange(len(text))
        kind = rng.random()
        if kind < 0.3:
            text = text[:at] + text[at + 1:]
        elif kind < 0.6:
            text = text[:at] + rng.choice(NOISE) + text[at:]
        elif kind < 0.75:
            other = rng.randrange(len(text))
            lo, hi = min(at, other), max(at, other)
            text = text[:lo] + text[hi:hi + 1] + text[lo + 1:hi] + text[lo:lo + 1] + text[hi + 1:]
        elif kind < 0.9:
            end = min(len(text), at + rng.randint(1, 12))
            text = text[:end] + text[at:end] + text[end:]
        else:
            text = text[:at]
    return text


VERSION = re.compile(rb"""^<\?xml[ \t\r\n]+version[ \t\r\n]*=[ \t\r\n]*(?:"([^"]*)"|'([^']*)')""")


def version_outside_grammar(data):
    """True when the document declares a version that is not VersionNum."""
    match = VERSION.match(data)
    if not match:
        return False
    value = match.group(1) if match.group(1) is not None else match.group(2)
    return re.fullmatch(rb"1\.[0-9]+", value) is None


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=os.path.join(REPO, "build", "litebird"))
    parser.add_argument("--count", type=int, default=2000)
    parser.add_argument("--seed", type=int, default=20261114)
    parser.add_argument("--show", type=int, default=5)
    parser.add_argument("--max-differ", type=int, default=0)
    parser.add_argument("--show-disagree", type=int, default=0,
                        help="print this many documents on which Expat and libxml2 disagree")
    parser.add_argument("--timeout", type=float, default=20)
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--expat-only", action="store_true",
                        help="run without libxml2; then only crashes and timeouts fail")
    args = parser.parse_args()
    if not os.path.exists(args.binary):
        sys.stderr.write("%s: no such file (run `make` first)\n" % args.binary)
        return 2
    if etree is None and not args.expat_only:
        sys.stderr.write("lxml is not installed (pip install lxml). Expat alone cannot be the reference:\n"
                         "it keeps the name characters of the editions before the fifth. --expat-only\n"
                         "runs with it all the same and fails on crashes and timeouts only.\n")
        return 2
    if etree is None:
        print("lxml is not installed: comparing with Expat alone, differences are printed and not counted")

    rng = random.Random(args.seed)
    documents = []
    for index in range(args.count):
        text = Generator(rng).document()
        if index % 2:
            text = damage(rng, text)
        documents.append(text.encode("utf-8", "surrogatepass"))

    with tempfile.TemporaryDirectory() as scratch:
        paths = []
        for index, data in enumerate(documents):
            path = os.path.join(scratch, "%05d.xml" % index)
            with open(path, "wb") as out:
                out.write(data)
            paths.append(path)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            ours = list(pool.map(lambda p: litebird_events(args.binary, p, args.timeout), paths))

    agree = differ = disagree = accepted = broken = by_grammar = 0
    with_expat = with_libxml2 = with_neither = 0
    shown = shown_disagree = 0
    for data, (mine, my_message) in zip(documents, ours):
        if mine in ("crash", "timeout"):
            broken += 1
            print("%s: %s\n    %r" % (mine.upper(), my_message, data[:300]))
            continue
        first, first_message = expat_events(data)
        second, second_message = lxml_events(data) if etree is not None else (first, first_message)
        if mine is not None:
            accepted += 1
        if version_outside_grammar(data):
            by_grammar += 1
            if mine is not None:
                differ += 1
                print("DIFFER: a version number that is not VersionNum was accepted: %r" % data[:200])
            continue
        if first != second:
            disagree += 1
            if mine == first:
                with_expat += 1
            elif mine == second:
                with_libxml2 += 1
            else:
                with_neither += 1
            if shown_disagree < args.show_disagree and (mine != first or args.show_disagree > 50):
                shown_disagree += 1
                side = "with Expat" if mine == first else "with libxml2" if mine == second else "with NEITHER"
                print("references disagree, Litebird %s: %r" % (side, data[:400]))
                for name, value, message in (("litebird", mine, my_message), ("expat", first, first_message),
                                             ("libxml2", second, second_message)):
                    print("    %-9s %s" % (name, "rejects: " + message[:150] if value is None else repr(value[:300])))
            continue
        if mine == first:
            agree += 1
            continue
        differ += 1
        if shown < args.show:
            shown += 1
            print("DIFFER: %r" % data[:400])
            for name, value, message in (("litebird", mine, my_message), ("expat", first, first_message),
                                         ("libxml2", second, second_message)):
                print("    %-9s %s" % (name, "rejects: " + message[:150] if value is None else repr(value[:300])))
    print("XML differential: %d documents (seed %d, half of them damaged), Litebird accepts %d; %d agree with both, "
          "%d DIFFER, %d where Expat and libxml2 disagree (Litebird is with Expat on %d, with libxml2 on %d, "
          "with neither on %d); %d with a version number outside the grammar, all refused%s"
          % (len(documents), args.seed, accepted, agree, differ, disagree, with_expat, with_libxml2, with_neither,
             by_grammar, "; %d crashes or timeouts" % broken if broken else ""))
    if broken:
        return 1
    if etree is None:
        return 0
    # With neither: Litebird reads a document one way, Expat another and
    # libxml2 a third. No such document is known; one would be worth a look.
    return 1 if differ > args.max_differ or with_neither else 0


if __name__ == "__main__":
    sys.exit(main())
