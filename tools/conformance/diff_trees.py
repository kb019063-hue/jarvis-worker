#!/usr/bin/env python3
"""Parses random pages with Litebird and with parse5 and lists the pages whose trees differ.

The corpus (`make conformance`) checks 1,792 inputs somebody wrote. This checks
pages nobody wrote: a few dozen start tags, end tags, comments and pieces of
text in random order, drawn from the elements the tree builder has rules of
its own for (tables, lists, formatting elements, buttons, templates, SVG and
MathML, framesets). Such a page is mostly misnested, which is where a tree
builder goes wrong. Both parsers print the tree in the format of the corpus
and the two texts are compared.

parse5 is the HTML parser Node.js tools use. It was written from the standard
by other people, so the two agreeing on a page is evidence, and the two
differing names a page a few dozen bytes long to look at.

Usage:
    python3 tools/conformance/diff_trees.py                     # summary, and the first 10 pages that differ
    python3 tools/conformance/diff_trees.py --reduce            # each of them cut down to the tags that matter
    python3 tools/conformance/diff_trees.py --pages 5000 --seed 7
    python3 tools/conformance/diff_trees.py --extras            # also <br/>, type=hidden, CDATA, U+0000 ...
    python3 tools/conformance/diff_trees.py --fragments         # as innerHTML of a random element
    python3 tools/conformance/diff_trees.py --no-scripting      # <noscript> is markup
    python3 tools/conformance/diff_trees.py --corrected         # against parse5 with the list below corrected
    python3 tools/conformance/diff_trees.py --tags "table tr td template form div b"
    python3 tools/conformance/diff_trees.py --baseline /tmp/old/build/litebird   # exit 1 if a page got worse
    python3 tools/conformance/diff_trees.py --max-differ 15     # exit 1 if more than 15 pages differ
    python3 tools/conformance/diff_trees.py --check-reference   # parse5 itself against the corpus

`make diff-trees` runs four of these (plain, --extras, and --extras with
--fragments, with and without scripting), each with --corrected, and fails if
a page differs. It needs `node`, and the first time `npm` and the network:
parse5 is installed into build/parse5. CI runs it in the conformance job.

The pages a seed gives depend on the options: --extras, --fragments and
--tags each make other pages. What a run cannot find is what its pages do not
contain. `<br/>` closing the paragraph it stands in was found the day
--extras wrote the first `<br/>`.

Where parse5 8.1 is not the standard
------------------------------------

A page that differs is not always Litebird's mistake. In each of these the
standard's text says what Litebird does, and a page that differs for one of
them reduces (--reduce) to something like the example.

- `<template><table><form>`, and `<form><form>` in a fragment for a
  `<template>`: inside a template there is no form pointer, so the form is
  kept. parse5 ignores it, which was the rule until September 2026.
- `<template></template><th><frameset>`: the body made for the first thing
  after the head sets the frameset-ok flag back to "ok" (same date).
- `<svg><title><![CDATA[x]]>`: a CDATA section is text wherever the current
  node is an SVG or MathML element. parse5 makes a comment of it in the
  elements whose content is HTML (`<title>`, `<desc>`, `<foreignObject>`,
  `<mi>` and the like).
- `<search>`: a special element in the standard, an ordinary one to parse5.
  Not in the built-in list of tags.
- `<table><template><tr><table>`: a `<template>` bounds table scope, so the
  second `<table>` finds no table to close and is ignored. parse5's table
  scope stops at `<table>` and `<html>` only.
- `<table><tr></thead> `: `</thead>` in a row is ignored unless a `<thead>`
  is open. parse5 closes the row if either a `<thead>` or a `<tr>` is.
- `<template><tr></tr><b><td></td></tr> `: whitespace directly in a template
  that holds rows is placed as it is. parse5 (and Blink) open the `<b>` again
  around it; the standard names the template among the elements text is
  collected in.
- `<svg>` followed by two U+0000: each is replaced by U+FFFD. parse5 makes
  one U+FFFD of a run of them.
- A fragment for a `<noscript>` with scripting disabled: markup in the
  standard, text to parse5 either way.

--corrected compares with a copy of parse5 in which these nine are changed to
what the standard says: CORRECTIONS below, a few lines each, applied to a
copy in build/parse5/corrected. With it a page that differs differs for some
other reason, which a ceiling on the count cannot tell. The price is that on
these nine rules the copy is no longer an independent witness: both sides were
written here from the same text. For the first four the web-platform-tests
copy of the corpus has cases (15 of them), which parse5 fails as published
and passes corrected. For table scope and `</thead>` in a row, Blink's source
does what the standard says. For U+0000 and the `<noscript>` fragment there
is the standard's text and nothing else, and the whitespace in a template is
the one on which Blink does what parse5 does.

Not corrected: `<select>`, `<optgroup>` and `<option>`. The standard changed
how the content of a `<select>` is parsed: Litebird now implements the current
rules and passes all 1,792 corpus cases, while parse5 8.1.1 keeps the older
rules. They are left out of the pages unless --with-select is given; with it
about one page in ten differs. --check-reference shows that `<select>` is the
only reason parse5 disagrees on 28 cases of the corpus.

What it does not tell
---------------------

Character references and the tokenizer states for scripts are not exercised;
the corpus is what covers those. Attributes are the few the tree builder
reads (--extras), not arbitrary ones. Nothing here runs a script, so what
`document.write` and a script that moves nodes during parsing do to the tree
is not compared.
"""
import argparse
import difflib
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

PARSE5_VERSION = "8.1.1"
HERE = os.path.dirname(os.path.abspath(__file__))

# parse5 8.1.1 where it is not the standard (see the list at the top): the
# file under node_modules/parse5/dist, the text to find there, which has to
# occur exactly once, and what replaces it. --corrected compares with a copy
# of parse5 that has these applied.
TEMPLATE_CONTENTS = ("(p.openElements.tmplCount > 0 || (p.fragmentContext != null && p.fragmentContextID === $.TEMPLATE"
                     " && p.treeAdapter.getNamespaceURI(p.fragmentContext) === NS.HTML))")
CORRECTIONS = [
    ("a form in the contents of a template: <form> in body",
     "parser/index.js",
     "function formStartTagInBody(p, token) {\n    const inTemplate = p.openElements.tmplCount > 0;",
     "function formStartTagInBody(p, token) {\n    const inTemplate = " + TEMPLATE_CONTENTS + ";"),
    ("a form in the contents of a template: </form>",
     "parser/index.js",
     "function formEndTagInBody(p) {\n    const inTemplate = p.openElements.tmplCount > 0;",
     "function formEndTagInBody(p) {\n    const inTemplate = " + TEMPLATE_CONTENTS + ";"),
    ("a form in the contents of a template: <form> in a table",
     "parser/index.js",
     "    if (!p.formElement && p.openElements.tmplCount === 0) {\n"
     "        p._insertElement(token, NS.HTML);\n"
     "        p.formElement = p.openElements.current;\n",
     "    const inTemplate = " + TEMPLATE_CONTENTS + ";\n"
     "    if (!p.formElement || inTemplate) {\n"
     "        p._insertElement(token, NS.HTML);\n"
     "        if (!inTemplate) p.formElement = p.openElements.current;\n"),
    ("the body made after the head sets the frameset-ok flag to ok",
     "parser/index.js",
     "function tokenAfterHead(p, token) {\n    p._insertFakeElement(TN.BODY, $.BODY);\n",
     "function tokenAfterHead(p, token) {\n    p._insertFakeElement(TN.BODY, $.BODY);\n    p.framesetOk = true;\n"),
    ("a template bounds table scope",
     "parser/open-element-stack.js",
     "KEY_CLASSES[$.HTML] |= TABLE_SCOPE;\n",
     "KEY_CLASSES[$.HTML] |= TABLE_SCOPE;\nKEY_CLASSES[$.TEMPLATE] |= TABLE_SCOPE;\n"),
    ("</tbody>, </thead> and </tfoot> in a row need their own element open",
     "parser/index.js",
     "if (p.openElements.hasInTableScope(token.tagID) || p.openElements.hasInTableScope($.TR)) {",
     "if (p.openElements.hasInTableScope(token.tagID) && p.openElements.hasInTableScope($.TR)) {"),
    ("text directly in a template that is in a table mode is collected like text in a table",
     "parser/index.js",
     "    if (p.openElements.currentTagId !== undefined && TABLE_STRUCTURE_TAGS.has(p.openElements.currentTagId)) {\n"
     "        p.pendingCharacterTokens.length = 0;\n",
     "    if (p.openElements.currentTagId !== undefined &&\n"
     "        (TABLE_STRUCTURE_TAGS.has(p.openElements.currentTagId) || p.openElements.currentTagId === $.TEMPLATE)) {\n"
     "        p.pendingCharacterTokens.length = 0;\n"),
    ("CDATA sections wherever the adjusted current node is not an HTML element: the tokenizer's flag",
     "tokenizer/index.js",
     "        this.inForeignNode = false;\n",
     "        this.inForeignNode = false;\n        this.allowCDATA = false;\n"),
    ("CDATA sections wherever the adjusted current node is not an HTML element: the tokenizer's test",
     "tokenizer/index.js",
     "            if (this.inForeignNode) {\n                this.state = State.CDATA_SECTION;\n",
     "            if (this.allowCDATA) {\n                this.state = State.CDATA_SECTION;\n"),
    ("CDATA sections wherever the adjusted current node is not an HTML element: the parser sets the flag",
     "parser/index.js",
     "        this.currentNotInHTML = !isHTML;\n",
     "        this.currentNotInHTML = !isHTML;\n        this.tokenizer.allowCDATA = !isHTML && current !== undefined;\n"),
    ("every U+0000 in SVG and MathML content becomes a U+FFFD",
     "parser/index.js",
     "    token.chars = unicode.REPLACEMENT_CHARACTER;\n    p._insertCharacters(token);\n",
     "    token.chars = unicode.REPLACEMENT_CHARACTER.repeat(token.chars.length);\n    p._insertCharacters(token);\n"),
    ("<search> is a special element",
     "common/html.js",
     "        $.SCRIPT,\n        $.SECTION,\n",
     "        $.SCRIPT,\n        $.SEARCH,\n        $.SECTION,\n"),
    ("a fragment for a <noscript> is text only when scripting is enabled",
     "parser/index.js",
     "            case $.NOFRAMES:\n            case $.NOSCRIPT: {\n"
     "                this.tokenizer.state = TokenizerMode.RAWTEXT;\n                break;\n            }\n",
     "            case $.NOFRAMES: {\n"
     "                this.tokenizer.state = TokenizerMode.RAWTEXT;\n                break;\n            }\n"
     "            case $.NOSCRIPT: {\n"
     "                if (this.options.scriptingEnabled) {\n"
     "                    this.tokenizer.state = TokenizerMode.RAWTEXT;\n"
     "                }\n                break;\n            }\n"),
]

# Elements with rules of their own in the tree builder. <div> is there four
# times so that ordinary blocks are common among them.
TAGS = ("div div div div span p b i a table tbody tr td th caption colgroup ul li dl dd dt form button "
        "h1 h2 svg g foreignObject desc math mi mtext annotation-xml template nobr em font "
        "frameset noscript object marquee applet title textarea pre listing center section ruby rt rb").split()
SELECT_TAGS = "select optgroup option".split()
VOID = "br img input hr col wbr meta link area".split()
STRAY = ["</table>", "</template>", "</svg>", "</math>", "</body>", "</html>", "<body>", "<html>"]
# With --extras: the few things other than a tag name that the tree builder
# looks at. Attributes it has a rule for, end tags that act as start tags, a
# start tag that closes itself, CDATA, a doctype out of place, U+0000.
EXTRAS = ["<input type=hidden>", "<input type=text>", "<font color=red>", "<font size=2>",
          "<annotation-xml encoding=text/html>", "<annotation-xml encoding=x>", "</br>", "</p>", "<br/>",
          "<svg/>", "<math/>", "<g/>", "<div/>", "<p/>", "<script/>", "<![CDATA[x]]>", "<!doctype html>",
          "\0", "<image>", "</sarcasm>", "<a href=x>", "<mglyph>", "<malignmark>"]
# With --fragments: the element a fragment is parsed as the content of, which
# is what assigning to innerHTML does. Written as in the corpus.
CONTEXTS = ("div div div p span a b table tbody thead tr td th caption colgroup template body html head form button "
            "ul li object frameset title textarea style script xmp iframe noembed noframes noscript plaintext").split() + [
            "svg svg", "svg g", "svg foreignObject", "svg desc", "svg title", "svg path", "svg script",
            "math math", "math mi", "math mo", "math mtext", "math annotation-xml"]


def random_page(rng: random.Random, tags: list[str], stray: list[str], extras: bool = False) -> str:
    """3 to 39 tokens. Six tags are drawn first and used for most of the
    page, so that end tags meet their start tags more often than chance.
    Without `extras` a seed gives the pages it always gave."""
    out = ["<!doctype html>"] if rng.random() < 0.8 else []
    favourites = rng.sample(tags, 6)
    for _ in range(rng.randrange(3, 40)):
        if extras and rng.random() < 0.15:
            out.append(rng.choice(EXTRAS))
            continue
        roll = rng.random()
        tag = rng.choice(favourites) if rng.random() < 0.6 else rng.choice(tags)
        if roll < 0.55:
            out.append(f"<{tag}>" if rng.random() < 0.9 else f"<{tag} a={rng.randrange(9)}>")
        elif roll < 0.75:
            out.append(f"</{tag}>")
        elif roll < 0.83:
            out.append(rng.choice(("x", " ", "text &amp; more", "\n")))
        elif roll < 0.90:
            out.append(f"<{rng.choice(VOID)}>")
        elif roll < 0.95:
            out.append("<!--c-->")
        else:
            out.append(rng.choice(stray))
    return "".join(out)


class Reference:
    """One node process that prints parse5's tree for each page it is named."""

    def __init__(self, parse5_dir: str) -> None:
        self.process = subprocess.Popen(["node", os.path.join(HERE, "parse5_tree.mjs"), parse5_dir],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)

    def tree(self, path: str, context: str = "", scripting: bool = True) -> str:
        """The tree of a document, or with `context` of a fragment parsed as the content of that element."""
        request = {"path": path}
        if context:
            request["context"] = context
        if not scripting:
            request["scripting"] = False
        self.process.stdin.write(json.dumps(request) + "\n")
        self.process.stdin.flush()
        if self.process.stdout.readline().strip() != "done":
            raise RuntimeError(f"parse5 gave no tree for {path}")
        with open(path + ".tree", encoding="utf-8", errors="surrogateescape", newline="") as handle:
            return handle.read()

    def close(self) -> None:
        self.process.stdin.close()
        self.process.wait()


def litebird_tree(binary: str, path: str, context: str = "", scripting: bool = True) -> str:
    """The tree, or a line saying how the process ended when it printed none."""
    command = [binary, "--dump-dom-webplatform"]
    if context:
        command.append(f"--fragment-context={context}")
    if not scripting:
        command.append("--scripting=off")
    try:
        done = subprocess.run(command + [path], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=60)
    except subprocess.TimeoutExpired:
        return "(no tree: still running after 60 seconds)\n"
    if done.returncode != 0:
        return f"(no tree: exit status {done.returncode})\n"
    return done.stdout.decode("utf-8", "surrogateescape")


def ensure_parse5(directory: str) -> str:
    """Installs parse5 into `directory` unless it is there. Returns its version, or '' with a message printed."""
    manifest = os.path.join(directory, "node_modules", "parse5", "package.json")
    if not os.path.exists(manifest):
        if not shutil.which("npm"):
            print(f"parse5 is not in {directory} and npm is not installed to fetch it", file=sys.stderr)
            return ""
        os.makedirs(directory, exist_ok=True)
        fetched = subprocess.run(["npm", "install", "--prefix", directory, "--no-save", "--no-audit", "--no-fund",
                                  "--silent", f"parse5@{PARSE5_VERSION}"])
        if fetched.returncode != 0 or not os.path.exists(manifest):
            print(f"npm could not install parse5@{PARSE5_VERSION} into {directory}", file=sys.stderr)
            return ""
    with open(manifest, encoding="utf-8") as handle:
        found = re.search(r'"version"\s*:\s*"([^"]+)"', handle.read())
    return found.group(1) if found else "of unknown version"


def corrected_parse5(directory: str) -> str:
    """Makes a copy of the parse5 in `directory` with CORRECTIONS applied, in
    `directory`/corrected, and returns that path, or '' with a message
    printed. The copy is made again when the corrections change. It is inside
    `directory` so that it finds the packages parse5 itself needs."""
    source = os.path.join(directory, "node_modules", "parse5")
    target_root = os.path.join(directory, "corrected")
    target = os.path.join(target_root, "node_modules", "parse5")
    stamp = os.path.join(target_root, "corrections.json")
    wanted = json.dumps(CORRECTIONS)
    if os.path.exists(stamp):
        with open(stamp, encoding="utf-8") as handle:
            if handle.read() == wanted:
                return target_root
    shutil.rmtree(target_root, ignore_errors=True)
    shutil.copytree(source, target)
    for title, name, old, new in CORRECTIONS:
        path = os.path.join(target, "dist", name)
        with open(path, encoding="utf-8", newline="") as handle:
            text = handle.read()
        if text.count(old) != 1:
            print(f"parse5 in {directory} is not the source the correction \"{title}\" was written for: "
                  f"the text it replaces occurs {text.count(old)} times in dist/{name}", file=sys.stderr)
            shutil.rmtree(target_root, ignore_errors=True)
            return ""
        with open(path, "w", encoding="utf-8", newline="") as handle:
            handle.write(text.replace(old, new))
    with open(stamp, "w", encoding="utf-8") as handle:
        handle.write(wanted)
    return target_root


def reduce_page(source: str, differs) -> str:
    """Takes tokens out one at a time for as long as the trees still differ,
    then attributes, then shortens text. What is left is not always the
    difference the page started with, but it is a difference."""
    tokens = re.findall(r"<[^>]*>|[^<]+", source)
    changed = True
    while changed:
        changed = False
        index = 0
        while index < len(tokens):
            shorter = tokens[:index] + tokens[index + 1:]
            if differs("".join(shorter)):
                tokens, changed = shorter, True
            else:
                index += 1
    for index, token in enumerate(tokens):
        simpler = re.sub(r" a=\d", "", token) if token.startswith("<") else "x"
        if simpler != token and differs("".join(tokens[:index] + [simpler] + tokens[index + 1:])):
            tokens[index] = simpler
    return "".join(tokens)


def check_reference(reference: Reference, version: str, scratch: str) -> int:
    """parse5 against the expected trees of the corpus: how far it can be trusted as a reference."""
    sys.path.insert(0, HERE)
    from pathlib import Path
    from run_tree_construction import DEFAULT_CORPUS, parse_dat

    total, unexpected = 0, []
    for dat in sorted(Path(DEFAULT_CORPUS).glob("*.dat")):
        for case in parse_dat(dat):
            path = os.path.join(scratch, f"case{total}.html")
            with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as out:
                out.write(case.data)
            total += 1
            # A case that does not say is right with scripting either way; try both, as the corpus runner does.
            modes = [case.scripting] if case.scripting is not None else [True, False]
            trees = (reference.tree(path, case.fragment_context or "", scripting) for scripting in modes)
            if not any(tree.rstrip("\n") == case.document.rstrip("\n") for tree in trees):
                unexpected.append((f"{dat.name} case {case.index}", case.data, case.fragment_context or ""))
    if total == 0:
        print(f"no corpus in {DEFAULT_CORPUS}; run this from the top of the repository", file=sys.stderr)
        return 2

    def about_select(data: str, context: str) -> bool:
        return "<select" in data.lower() or context in ("select", "optgroup", "option")

    others = [name for name, data, context in unexpected if not about_select(data, context)]
    print(f"parse5 {version} against the corpus: {total - len(unexpected)} of {total} cases give the expected tree; "
          f"of the other {len(unexpected)}, {len(unexpected) - len(others)} contain <select> or are fragments for one")
    for name in others:
        print(f"  without <select>: {name}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--binary", default="build/litebird", help="the Litebird binary (default: build/litebird)")
    parser.add_argument("--baseline", default="", metavar="BINARY",
                        help="another Litebird binary: report the pages on which the two disagree about "
                             "equalling parse5, and exit 1 if --binary lost any")
    parser.add_argument("--pages", type=int, default=1500, help="how many pages (default 1500)")
    parser.add_argument("--seed", type=int, default=20261110, help="seed of the pages (default 20261110)")
    parser.add_argument("--with-select", action="store_true",
                        help="use <select>, <optgroup> and <option> too (parse5 parses these by older rules)")
    parser.add_argument("--fragments", action="store_true",
                        help="parse each page as a fragment, in an element drawn from a list of contexts "
                             "(what innerHTML does) instead of as a document")
    parser.add_argument("--no-scripting", action="store_true",
                        help="parse with scripting disabled, which changes what a <noscript> is")
    parser.add_argument("--extras", action="store_true",
                        help="also draw attributes the tree builder reads (type=hidden, encoding=, color=), "
                             "self-closing tags, </br> and </p>, CDATA, a stray doctype and U+0000")
    parser.add_argument("--tags", default="", metavar="NAMES",
                        help="the elements to make pages from, separated by spaces, instead of the built-in list; "
                             "a name given twice is drawn twice as often")
    parser.add_argument("--show", type=int, default=10, help="how many differing pages to print (default 10)")
    parser.add_argument("--reduce", action="store_true",
                        help="cut every differing page down to the tokens that matter and list the distinct results")
    parser.add_argument("--max-differ", type=int, default=-1, help="exit 1 if more pages than this differ")
    parser.add_argument("--corrected", action="store_true",
                        help="compare with a copy of parse5 in which its known departures from the standard "
                             "are corrected (the list at the top of this file) instead of with parse5 as published")
    parser.add_argument("--parse5-dir", default="build/parse5",
                        help="where parse5 is installed, or is to be (default: build/parse5)")
    parser.add_argument("--check-reference", action="store_true",
                        help="instead: run parse5 over the document cases of the corpus and count the expected trees")
    args = parser.parse_args()

    if not shutil.which("node"):
        print("node is not installed; there is nothing to compare with", file=sys.stderr)
        return 2
    binaries = [args.binary] + ([args.baseline] if args.baseline else [])
    for binary in binaries:
        if not args.check_reference and not os.path.exists(binary):
            print(f"{binary} does not exist; run `make` first", file=sys.stderr)
            return 2
    version = ensure_parse5(args.parse5_dir)
    if not version:
        return 2
    parse5_dir = args.parse5_dir
    if args.corrected:
        if version != PARSE5_VERSION:
            print(f"--corrected is written for parse5 {PARSE5_VERSION}; {args.parse5_dir} holds {version}", file=sys.stderr)
            return 2
        parse5_dir = corrected_parse5(args.parse5_dir)
        if not parse5_dir:
            return 2
        version += ", corrected"

    tags = args.tags.split() or TAGS
    if args.with_select:
        tags = tags + SELECT_TAGS
    if len(tags) < 6:
        print("--tags needs at least six names: six are drawn first for each page", file=sys.stderr)
        return 2
    stray = STRAY + ["</select>"] if args.with_select else STRAY
    rng = random.Random(args.seed)
    reference = Reference(parse5_dir)
    published = Reference(args.parse5_dir) if args.corrected else None
    try:
        with tempfile.TemporaryDirectory() as scratch:
            if args.check_reference:
                return check_reference(reference, version, scratch)

            scripting = not args.no_scripting

            def trees_of(name: str, source: str, context: str) -> tuple[str, list[str]]:
                path = os.path.join(scratch, name)
                with open(path, "w", encoding="ascii", newline="") as out:
                    out.write(source)
                return (reference.tree(path, context, scripting),
                        [litebird_tree(binary, path, context, scripting) for binary in binaries])

            differing, lost, gained, baseline_equal, published_equal = [], [], [], 0, 0
            for number in range(args.pages):
                source = random_page(rng, tags, stray, args.extras)
                context = rng.choice(CONTEXTS) if args.fragments else ""
                expected, actual = trees_of(f"page{number}.html", source, context)
                equal = actual[0] == expected
                if published is not None:
                    path = os.path.join(scratch, f"page{number}.html")
                    published_equal += published.tree(path, context, scripting) == actual[0]
                if not equal:
                    differing.append((number, source, context, expected, actual[0]))
                if args.baseline:
                    was_equal = actual[1] == expected
                    baseline_equal += was_equal
                    if was_equal and not equal:
                        lost.append((number, source, context))
                    elif equal and not was_equal:
                        gained.append((number, source, context))

            def shown(source: str, context: str) -> str:
                return f"{source!r} in <{context}>" if context else repr(source)

            what = "with <select>" if args.with_select else "no <select>"
            if args.extras:
                what += ", with the extras"
            if args.fragments:
                what += ", as fragments"
            if args.no_scripting:
                what += ", scripting disabled"
            print(f"{args.pages} random pages (seed {args.seed}, 3 to 39 tokens each, {what}): "
                  f"{args.pages - len(differing)} trees equal to those of parse5 {version}, {len(differing)} differ"
                  + (f"; {published_equal} equal to those of parse5 as published" if published is not None else ""))
            if args.baseline:
                print(f"{args.baseline}: {baseline_equal} equal; {len(gained)} pages equal now and not with it, "
                      f"{len(lost)} the other way")
                for number, source, context in lost[:max(args.show, 0)]:
                    print(f"  lost: page {number}: {shown(source, context)}")

            for number, source, context, expected, actual in differing[:max(args.show, 0)]:
                print(f"\npage {number}: {shown(source, context)}")
                changes = [line for line in difflib.unified_diff(expected.split("\n"), actual.split("\n"),
                                                                 "parse5", "litebird", lineterm="", n=1)]
                for line in changes[:14]:
                    print(f"    {line}")
                if len(changes) > 14:
                    print(f"    ... {len(changes) - 14} more lines")

            if args.reduce and differing:
                reduced: dict[tuple[str, str], list[int]] = {}
                for number, source, context, _, _ in differing:
                    def differs(shorter: str) -> bool:
                        expected, actual = trees_of("reduced.html", shorter, context)
                        return actual[0] != expected

                    reduced.setdefault((reduce_page(source, differs), context), []).append(number)
                print(f"\nreduced, {len(reduced)} distinct:")
                for (source, context), numbers in sorted(reduced.items(), key=lambda item: (len(item[0][0]), item[0])):
                    pages = "page " + str(numbers[0]) if len(numbers) == 1 else "pages " + ", ".join(map(str, numbers))
                    print(f"  {shown(source, context)}  ({pages})")
    finally:
        reference.close()
        if published is not None:
            published.close()

    if lost:
        print(f"\n{len(lost)} pages equalled parse5 with {args.baseline} and no longer do", file=sys.stderr)
        return 1
    if args.max_differ >= 0 and len(differing) > args.max_differ:
        print(f"\nmore than {args.max_differ} pages differ", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
