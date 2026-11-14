#!/usr/bin/env python3
"""Pages that nest one thing far deeper than the native stack is long.

The mutation fuzzers start from the html5lib inputs, a few lines each, and
change them a few bytes at a time, so they cannot find the oldest way to end a
process from a web page: recursion. Every walk over a tree that calls itself
once per level (a destructor, a serializer, a selector search, layout,
painting) works for any page a person wrote and dies on a page a program
wrote. This generates those pages. Each one nests a single construct `--depth`
levels and is run through the engine. At the default depth of 200,000 on the
default 8 MB stack, a walk that uses as little as 42 bytes of stack per level
cannot finish.

Not every page here is deep. Some are long or large instead: one paragraph
with 200,000 children, 200,000 elements left open and then 100,000 tokens, one
parent given 200,000 children by script, a canvas or an SVG larger than
memory. They were added as the deep pages ran into them. Work that grows with
the square of the input fails the time limit the way recursion fails the
stack, and neither is found by mutating a small page.

A run passes when

  * the process exits 0: no signal, no sanitizer report;
  * it finishes inside the time and memory limits, and its output is bounded
    (work or output that grows with the square of the depth does not finish).
    The output limit is generous on purpose: a tree dump indents by depth, so
    at the parser's nesting limit a line is a kilobyte long and the dumps of
    a page with 800,000 nodes come to 1.7 GB. That is linear. What the limit
    is there to stop was 800 MB at 20,000 levels and growing with the square;
  * the text after the deep part is in the tree, laid out and painted, which
    shows that the engine went on working;
  * for a page the HTML parser builds: the text at the very bottom of the nest
    is in the tree, laid out and painted too. The parser attaches no element
    deeper than 512 (HtmlTreeBuilder::kMaxTreeDepth), so nothing in a parsed
    tree is out of layout's reach;
  * for a page that builds its tree from script: the last script prints the
    values it is expected to compute (the length of innerHTML, the number of
    matches of a selector, the name of an exception, ...).

Three kinds of run per page:

  dumps      --dump-dom --dump-layout --dump-paint --render-bmp, in one process
  terminal   the default mode: the page rendered as text
  html5lib   --dump-dom-webplatform: the parser alone (it runs no scripts, so
             only pages without script get this run)

With --random N, N more pages are generated from a seed: a few thousand start
tags, end tags, comments and text runs drawn from the tags the tree builder
treats specially (tables, templates, select, formatting elements, SVG and
MathML), biased towards start tags so that the parser spends most of the page
at its nesting limit. Those runs check for survival and that no element ends
up deeper than the limit allows.

Usage:
    python3 tools/fuzz/deep_pages.py                    # every page, build/litebird
    python3 tools/fuzz/deep_pages.py --only table       # pages whose name contains "table"
    python3 tools/fuzz/deep_pages.py --depth 300        # a depth any stack holds: checks the checks
    python3 tools/fuzz/deep_pages.py --random 500       # 500 random pages at the nesting limit as well
    python3 tools/fuzz/deep_pages.py --list
    python3 tools/fuzz/deep_pages.py --keep /tmp/pages  # leave the generated pages there

A sanitizer build has stack frames an order of magnitude larger and runs
several times slower. It is run for memory errors on these pages, not to find
recursion, so give it less depth and a small stack, and do not ask it to lay
out the bottom of the nest (layout stops when the stack is nearly used up, and
with frames that large it is used up early). `make asan-deep` runs:

    python3 tools/fuzz/deep_pages.py --binary build-asan/litebird \\
        --depth 10000 --stack-mb 2 --no-layout-check --timeout 300
"""

from __future__ import annotations

import argparse
import concurrent.futures
import os
import random
import re
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

DEFAULT_BINARY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "litebird")

BOTTOM = "DEEPBOTTOM"       # text at the innermost level of the nest
AFTER = "DEEPAFTER"         # text in a shallow paragraph after it
SCRIPT_OK = "DEEP-SCRIPT"   # prefix of the line a page's last script prints

# HtmlTreeBuilder::kMaxTreeDepth. No element the parser attaches is deeper.
MAX_TREE_DEPTH = 512

RUNS = {
    "dumps": ["--dump-dom", "--dump-layout", "--dump-paint", "--render-bmp", "{bmp}"],
    "terminal": ["--render-terminal"],
    "html5lib": ["--dump-dom-webplatform"],
}

# How each stage of the pipeline writes a piece of text in the "dumps" run.
IN_TREE = '#text: "%s'
LAID_OUT = 'text="%s'
PAINTED = ') "%s'


class Page:
    def __init__(self, name: str, html: str, *, bottom: bool = False, after: bool = True,
                 after_is_deep: bool = False, expect: str | None = None, max_depth: int | None = None) -> None:
        self.name = name
        self.html = html
        self.bottom = bottom        # the innermost text must reach the tree, layout and paint
        self.after = after          # so must the text after the nest
        self.after_is_deep = after_is_deep  # ... which is itself at the nesting limit in this page
        self.expect = expect        # what the last script prints after SCRIPT_OK
        self.max_depth = max_depth  # deepest element allowed in the html5lib dump
        self.has_script = "<script" in html


def document(body: str, head: str = "") -> str:
    return f"<!doctype html><html><head><title>deep</title>{head}</head><body>{body}<p>{AFTER}</p></body></html>"


def nest(open_tag: str, close_tag: str, n: int, inner: str = BOTTOM) -> str:
    return open_tag * n + inner + close_tag * n


def script(code: str) -> str:
    return f"<script>{code}</script>"


def report(*values: str) -> str:
    """The last line of a script page: SCRIPT_OK and the values it computed."""
    return "console.log(" + ", ".join([repr(SCRIPT_OK)] + list(values)) + ");"


# --- Pages the HTML parser builds -------------------------------------------------

def parsed_pages(n: int) -> list[Page]:
    def deep(name: str, body: str, head: str = "", **options) -> Page:
        options.setdefault("bottom", True)
        return Page(name, document(body, head), **options)

    def unclosed(name: str, body: str, **options) -> Page:
        """A nest of inline elements that is never closed. The paragraph that
        every page carries goes before it: after it, the paragraph would be a
        block inside an inline box, and layout loses those at any depth
        (AUDIT.md, finding 28), which is not what this tool is looking for."""
        options.setdefault("bottom", True)
        return Page(name, f"<!doctype html><title>deep</title><p>{AFTER}</p>" + body, **options)

    at_limit = "<div>" * (MAX_TREE_DEPTH + 100)
    pages = [
        deep("div", nest("<div>", "</div>", n)),
        unclosed("div, never closed", "<div>" * n + BOTTOM),
        deep("span", nest("<span>", "</span>", n)),
        deep("div and span alternating", nest("<div><span>", "</span></div>", n)),
        deep("b (formatting element)", nest("<b>", "</b>", n)),
        unclosed("b with a different attribute each time", "".join(f"<b id=b{i}>" for i in range(min(n, 2000))) + BOTTOM),
        unclosed("i and p interleaved", "<i><p>" * n + BOTTOM),
        unclosed("a inside a", "<a href=#>" * n + BOTTOM),
        deep("ul li", nest("<ul><li>", "</li></ul>", n)),
        deep("ol li", nest("<ol><li>", "</li></ol>", n)),
        deep("dl dd", nest("<dl><dd>", "</dd></dl>", n)),
        deep("blockquote", nest("<blockquote>", "</blockquote>", n)),
        deep("table", nest("<table><tr><td>", "</td></tr></table>", n)),
        deep("table with caption", nest("<table><caption>", "</caption></table>", n)),
        deep("table, text straight inside", "<table>" * n + BOTTOM),
        # Nothing closes these nests, so the paragraph after them is in them,
        # at the limit.
        deep("tables with fostered text at the limit", at_limit + f"<table>{BOTTOM}<b>x" * min(n, 20000),
             bottom=False, after_is_deep=True),
        deep("formatting misnested at the limit", at_limit + "<b><i><p>x</b></i>" * min(n, 20000) + BOTTOM,
             bottom=False, after_is_deep=True),
        deep("comments and void elements at the limit", at_limit + "<!--c--><br><img><div>" * min(n, 50000) + BOTTOM,
             bottom=False, after_is_deep=True),
        deep("details", nest("<details open><summary>s</summary>", "</details>", n)),
        deep("button", nest("<div><button>", "</button></div>", n)),
        deep("fieldset", nest("<fieldset><legend>l</legend>", "</fieldset>", n)),
        deep("svg g", "<svg width=10 height=10>" + nest("<g>", "</g>", n, "<rect width=1 height=1 />") + "</svg>", bottom=False),
        deep("svg inside svg", nest("<svg>", "</svg>", n, "<rect width=1 height=1 />"), bottom=False),
        deep("svg foreignObject", "<svg>" + nest("<foreignObject><svg>", "</svg></foreignObject>", n, "<rect/>") + "</svg>", bottom=False),
        deep("math mrow", "<math>" + nest("<mrow>", "</mrow>", n, f"<mi>{BOTTOM}</mi>") + "</math>"),
        deep("template", nest("<template>", "</template>", n), bottom=False),
        deep("template in table", nest("<table><template>", "</template></table>", n), bottom=False),
        deep("select optgroup", "<select>" + "<optgroup>" * n + "<option>o</option></select>", bottom=False),
        deep("flex containers", nest('<div style="display:flex">', "</div>", n)),
        deep("grid containers", nest('<div style="display:grid">', "</div>", n)),
        deep("floats", nest('<div style="float:left">', "</div>", n)),
        deep("inline-block", nest('<div style="display:inline-block">', "</div>", n)),
        deep("overflow hidden", nest('<div style="overflow:hidden">', "</div>", n)),
        deep("positioned", nest('<div style="position:relative">', "</div>", n)),
        deep("absolutely positioned", nest('<div style="position:absolute">', "</div>", n), bottom=False),
        deep("transformed", nest('<div style="transform:translate(1px,1px)">', "</div>", n)),
        deep("padding and border", nest('<div style="padding:1px;border:1px solid red">', "</div>", n)),
        deep("percentage widths", nest('<div style="width:99%">', "</div>", n)),
        deep("comments between", nest("<div><!--c-->", "</div>", n)),
        deep("style rule on every level", nest('<div class="d">', "</div>", n),
             "<style>.d { color: red } div div { margin: 0 } div > div { padding: 0 }</style>"),
        deep("descendant selector as long as the nest", nest("<div>", "</div>", min(n, 2000)),
             "<style>" + "div " * min(n, 2000) + "{ color: red }</style>"),
        deep("noscript", nest("<noscript>", "</noscript>", n), bottom=False),
        deep("attributes with quotes", nest('<div title="&quot;&lt;">', "</div>", n)),
        Page("frameset", "<!doctype html><title>deep</title>" + "<frameset>" * n + "<frame>" + "</frameset>" * n, after=False),
        deep("h1 to h3", nest("<h1><h2><h3>", "</h3></h2></h1>", n)),
        unclosed("p inside button inside p", "<p><button>" * n + BOTTOM),
        unclosed("nobr", "<nobr>" * n + BOTTOM),
        deep("ruby", nest("<ruby><rb>", "</rb></ruby>", n)),
        deep("unknown elements", nest("<x-y>", "</x-y>", n)),
        # Not deep, but found by the deep SVG pages: every <svg> and <canvas>
        # is drawn into a bitmap as large as the element says it is.
        deep("svg larger than memory", f'<svg width="100000" height="100000"><rect width="5" height="5"/></svg>{BOTTOM}'),
        deep("svg with a size no vector can have", f'<svg width="1e9" height="1e9"></svg>{BOTTOM}'),
        deep("svg sized by a style", f'<svg style="width:100000px;height:100000px"></svg>{BOTTOM}'),
        deep("canvas larger than memory", f'<canvas width="100000" height="100000"></canvas>{BOTTOM}'),
        deep("svg, 3.6 GB of bitmaps at 180 KB each", "<svg></svg>" * min(n, 20000) + BOTTOM),
    ]

    # Many elements left open, then many tokens. The stack of open elements
    # is as long as the markup is deep (the nesting limit bounds the tree, not
    # the stack), and the standard answers several questions by walking down
    # it: which <li> a new <li> closes, what an end tag closes, which table an
    # element is in. A walk per token over 200,000 open elements does not
    # finish in a minute.
    then = min(n, 100000)
    after_open = {
        "bold text": ("<div>", "<b>x</b>"),
        "tables": ("<div>", "<table></table>"),
        "list items": ("<div>", "<li>x</li>"),
        "definition items": ("<div>", "<dd>x</dd><dt>y</dt>"),
        "end tags that close nothing": ("<span>", "</x>"),
        "table end tags that close nothing": ("<div>", "</td></tr></table>"),
        "paragraphs": ("<div>", "<p>x"),
        "links": ("<div>", "<a href=#>x"),
        "misnested formatting": ("<div>", "<b><p>x</b>"),
        "forms closed too early": ("<div>", "<form><div></form>"),
        "selects": ("<div>", "<select><option>x</select>"),
        "svg": ("<div>", "<svg><g></g></svg>"),
        "templates": ("<div>", "<template>x</template>"),
        "headings": ("<div>", "<h1>x</h1>"),
    }
    for name, (open_tag, token) in after_open.items():
        pages.append(unclosed(f"open elements, then {name}", open_tag * n + token * then, bottom=False))

    # Long, not deep: one paragraph with very many children. After breaking
    # the lines, layout tells each child where its pieces went. When each
    # child searched every line for its own, 100,000 images in a paragraph
    # took half a minute, and 100,000 words with an inline element between
    # each two took over two minutes.
    #
    # The two with an inline element per word are half as long as the others:
    # a word and an inline element with a word in it cost about 13 KB of
    # memory between them (a computed style is 920 bytes and every box, line
    # item and fragment has its own copy; AUDIT.md, finding 12), so 200,000
    # of them pass 2 GB.
    in_one_paragraph = {
        "images": ("<img width=10 height=10>", 200000),
        "line breaks": ("a<br>", 200000),
        "svg": ("<svg width=10 height=10></svg>", 200000),
        "words and inline elements": ("word <b>bold</b> ", 100000),
        "links": ("<a href=#>link</a>, ", 100000),
    }
    for name, (item, most) in in_one_paragraph.items():
        pages.append(deep(f"one paragraph, many {name}", f"<p>{item * min(n, most)}<span>{BOTTOM}</span></p>"))

    values = {
        "calc nested": f"width: {'calc(' * n}1px{')' * n}",
        "calc nested, never closed": f"width: {'calc(' * n}1px",
        "calc, one, never closed": "width: calc(1px",
        "calc with nested parentheses": f"width: calc({'(' * n}1px{')' * n})",
        "calc with a run of signs": f"width: calc({'-' * n}1px)",
        "calc with a long sum": f"width: calc({'1px + ' * n}1px)",
        "var nested fallbacks": f"color: {'var(--x, ' * n}red{')' * n}",
        "var nested, never closed": f"color: {'var(--x, ' * n}red",
        "parentheses": f"width: {'(' * n}1px{')' * n}",
        "functions": f"color: {'rgb(' * n}0{')' * n}",
        "brackets": f"grid-template-areas: {'[' * n}a{']' * n}",
    }
    for name, declaration in values.items():
        pages.append(deep(f"css {name}, in a style attribute", f'<div style="{declaration}">{BOTTOM}</div>'))
        pages.append(deep(f"css {name}, in a style sheet", f"<div>{BOTTOM}</div>", f"<style>div {{ {declaration} }}</style>"))
    sheet = {
        "var chain": ":root { " + " ".join(f"--v{i}: var(--v{i + 1});" for i in range(n)) + f" --v{n}: red; }} div {{ color: var(--v0) }}",
        "var cycle": ":root { --a: var(--b); --b: var(--a); } div { color: var(--a) }",
        ":not nested": f"div{':not(' * n}p{')' * n} {{ color: red }}",
        "blocks nested": f"{'@media screen {' * n} div {{ color: red }} {'}' * n}",
        "braces, never closed": "a {" * n,
        "selector with many combinators": f"{'div > ' * n}div {{ color: red }} {'div ~ ' * n}div {{ color: red }}",
        "attribute selectors": "div" + "[a]" * n + " { color: red }",
    }
    for name, css in sheet.items():
        pages.append(deep(f"css {name}", f"<div>{BOTTOM}</div>", f"<style>{css}</style>"))
    return pages


# --- Pages that build the tree from script ------------------------------------------

def chain(n: int, tag: str = "div", style: str = "") -> str:
    """Script that builds `top`, a chain of n elements, with `at` the innermost."""
    styled = f"d.setAttribute('style', '{style}');" if style else ""
    return (f"var top = document.createElement('{tag}'), at = top;"
            f"for (var i = 1; i < {n}; i++) {{ var d = document.createElement('{tag}'); {styled} at.appendChild(d); at = d; }}"
            f"at.appendChild(document.createTextNode('{BOTTOM}'));")


def xml_pages(n: int, page) -> list[Page]:
    """XML handed to DOMParser, innerHTML of an XML document, XMLSerializer
    and XMLHttpRequest.

    The XML parser is a second parser and the XML serializer a second walk
    over a tree, each with every way to go wrong the HTML ones had: recursion
    per level, a search of all attributes per attribute, a copy of the
    namespace scope per element. And XML brings one of its own: entities
    that expand to entities.
    """
    wide = min(n, 50000)            # attributes, or declarations, on one element
    parse = "function xml(t) { return new DOMParser().parseFromString(t, 'application/xml'); }"
    write = "function text(node) { return new XMLSerializer().serializeToString(node); }"
    nested = f"var src = '<a>'.repeat({n}) + '{BOTTOM}' + '</a>'.repeat({n}); var d = xml(src);"
    get = "function get(url) { var x = new XMLHttpRequest(); x.open('GET', url, false); x.send(); return x; }"
    depth = "var depth = 0; for (var e = d.documentElement; e && e.nodeType === 1; e = e.firstChild) depth++;"
    laughs = ("var dtd = '<!DOCTYPE l [<!ENTITY a \"aaaaaaaaaa\">', names = 'abcdefghij';"
              "for (var i = 1; i < 10; i++) { var r = ''; for (var k = 0; k < 10; k++) r += '&' + names[i - 1] + ';';"
              " dtd += '<!ENTITY ' + names[i] + ' \"' + r + '\">'; }")
    return [
        page("xml: a deep document parsed and written back", parse + write + nested + depth,
             ["depth", "text(d) === src", "d.getElementsByTagName('a').length", "d.documentElement.textContent"],
             f"{n} true {n} {BOTTOM}"),
        page("xml: a deep document dropped", parse + nested + "d = null; if (typeof gc === 'function') gc();", ["'dropped'"], "dropped"),
        page("xml: a deep document cloned, compared and imported",
             parse + nested + "var copy = d.cloneNode(true), mine = document.importNode(d.documentElement, true);",
             ["copy.isEqualNode(d)", "copy.documentElement !== d.documentElement", "mine.ownerDocument === document",
              "mine.isEqualNode(d.documentElement)"], "true true true true"),
        page("xml: a deep document left open", parse + f"var d = xml('<a>'.repeat({n}) + '{BOTTOM}');",
             ["d.documentElement.nodeName", "d.getElementsByTagName('a').length", "d.childNodes.length"], "parsererror 0 1"),
        page("xml: a deep tree built by script, written and read back",
             parse + write +
             "var X = document.implementation.createDocument(null, 'r'), at = X.documentElement;"
             f"for (var i = 1; i < {n}; i++) {{ var c = X.createElementNS(i % 2 ? 'urn:a' : 'urn:b', 'p:e'); at.appendChild(c); at = c; }}"
             f"at.textContent = '{BOTTOM}'; var d = xml(text(X)), same = true, i = 0;"
             "for (var e = d.documentElement; e && e.nodeType === 1; e = e.firstChild, i++) {"
             " if (i > 0 && e.namespaceURI !== (i % 2 ? 'urn:a' : 'urn:b')) same = false; }",
             ["i", "same", "d.documentElement.textContent", "X.documentElement.innerHTML.length > " + str(n)],
             f"{n} true {BOTTOM} true"),
        page("xml: one prefix declared again at every level",
             parse + write +
             f"var src = ''; for (var i = 0; i < {n}; i++) src += '<p:a xmlns:p=\"urn:' + (i % 2) + '\">';"
             f"src += '{BOTTOM}' + '</p:a>'.repeat({n}); var d = xml(src), same = true, i = 0;"
             "for (var e = d.documentElement; e && e.nodeType === 1; e = e.firstChild, i++) { if (e.namespaceURI !== 'urn:' + (i % 2)) same = false; }",
             ["i", "same", "text(d) === src"], f"{n} true true"),
        page("xml: innerHTML of an XML document set to a deep nest",
             parse + f"var d = xml('<r xmlns=\"urn:d\"/>'), r = d.documentElement; r.innerHTML = '<a>'.repeat({n}) + '{BOTTOM}' + '</a>'.repeat({n});"
             "var depth = 0; for (var e = r.firstChild; e && e.nodeType === 1; e = e.firstChild) depth++;",
             ["depth", "r.innerHTML.length", "r.firstChild.namespaceURI", "r.textContent"],
             f"{n} {17 + 3 * (n - 1) + len(BOTTOM) + 4 * n} urn:d {BOTTOM}"),
        page("xml: namespaces looked up from the bottom",
             parse + f"var d = xml('<r xmlns:p=\"urn:p\" xmlns=\"urn:d\">' + '<a>'.repeat({n}) + '<p:b/>' + '</a>'.repeat({n}) + '</r>');"
             "var at = d.documentElement; while (at.firstChild) at = at.firstChild;",
             ["at.lookupNamespaceURI('p')", "at.lookupNamespaceURI(null)", "at.lookupPrefix('urn:p')", "at.isDefaultNamespace('urn:d')",
              "d.querySelectorAll('a').length", "d.querySelector('*|b') === at", "at.closest('r') === d.documentElement"],
             f"urn:p urn:d p true {n} true true"),
        page("xml: one element with very many attributes",
             parse + write +
             f"var src = '<a'; for (var i = 0; i < {wide}; i++) src += ' a' + i + '=\"v\"'; src += '/>'; var d = xml(src), e = d.documentElement;"
             "var twice = xml(src.slice(0, -2) + ' a0=\"again\"/>');",
             ["e.attributes.length", "text(d) === src", f"e.getAttribute('a{wide - 1}')", "twice.documentElement.nodeName"],
             f"{wide} true v parsererror"),
        page("xml: very many namespace declarations, each used once",
             parse + write +
             f"var src = '<r'; for (var i = 0; i < {wide}; i++) src += ' xmlns:p' + i + '=\"urn:n' + i + '\"'; src += '>';"
             f"for (var i = 0; i < {wide}; i++) src += '<p' + i + ':c/>'; src += '</r>'; var d = xml(src), r = d.documentElement;",
             ["r.childNodes.length", "r.lastChild.namespaceURI", "text(d) === src"], f"{wide} urn:n{wide - 1} true"),
        page("xml: very many attributes with namespaces set by script",
             write +
             "var X = document.implementation.createDocument(null, 'r'), r = X.documentElement;"
             f"for (var i = 0; i < {wide}; i++) r.setAttributeNS('urn:n' + (i % 50), 'p' + (i % 50) + ':a' + i, i);"
             f"var found = 0; for (var i = 0; i < {wide}; i++) if (r.getAttributeNS('urn:n' + (i % 50), 'a' + i) === String(i)) found++;",
             ["found", "r.attributes.length", "text(X).length > " + str(wide * 10)], f"{wide} {wide} true"),
        page("xml: a long run of comments, instructions and CDATA",
             parse + write +
             f"var src = '<r>' + ('<' + '!--c--><?p d?><![CDATA[x]]><e/>').repeat({n // 4}) + '</r>'; var d = xml(src);",
             ["d.documentElement.childNodes.length", "text(d) === src"], f"{4 * (n // 4)} true"),
        page("xml: entities that expand to entities",
             parse + laughs +
             "var inText = xml(dtd + ']><l>&j;</l>'), inValue = xml(dtd + ']><l b=\"&j;\"/>');"
             f"var long = xml('<!DOCTYPE q [<!ENTITY e \"' + 'x'.repeat(50000) + '\">]><q>' + '&e;'.repeat(50000) + '</q>');",
             ["inText.documentElement.nodeName", "inValue.documentElement.nodeName", "long.documentElement.nodeName"],
             "parsererror parsererror parsererror"),
        page("xml: a short entity used very many times",
             parse + f"var d = xml('<!DOCTYPE q [<!ENTITY e \"xxxxxxxxxx\">]><q>' + '&e;'.repeat({n}) + '</q>');",
             ["d.documentElement.nodeName", "d.documentElement.textContent.length"], f"q {10 * n}"),
        page("xml: a DOCTYPE with very many declarations",
             parse +
             f"var list = '<!DOCTYPE a [<!ATTLIST a'; for (var i = 0; i < {wide}; i++) list += ' a' + i + ' CDATA \"v\"';"
             "var declared = xml(list + '>]><a/>');"
             f"var again = '<!DOCTYPE a ['; for (var i = 0; i < {wide}; i++) again += '<!ATTLIST a x CDATA \"v' + i + '\">';"
             "var first = xml(again + ']><a/>');"
             f"var model = xml('<!DOCTYPE a [<!ELEMENT a ' + '('.repeat({n}) + 'b' + ')'.repeat({n}) + '>]><a/>');"
             f"var chain = '<!DOCTYPE a [<!ENTITY e0 \"x\">'; for (var i = 1; i < {wide}; i++) chain += '<!ENTITY e' + i + ' \"&e' + (i - 1) + ';\">';"
             f"var linked = xml(chain + ']><a>&e{wide - 1};</a>');",
             ["declared.documentElement.attributes.length", "first.documentElement.getAttribute('x')",
              "model.documentElement.nodeName", "linked.documentElement.textContent"],
             f"{wide} v0 a x"),
        page("xml: attribute defaults on very many elements",
             parse +
             f"var long = xml('<!DOCTYPE a [<!ATTLIST e x CDATA \"' + 'v'.repeat(1000000) + '\">]><a>' + '<e/>'.repeat({n}) + '</a>');"
             f"var small = xml('<!DOCTYPE a [<!ATTLIST e x CDATA \"v\">]><a>' + '<e/>'.repeat({n}) + '</a>');",
             ["long.documentElement.nodeName", "small.documentElement.childNodes.length", "small.documentElement.lastChild.getAttribute('x')"],
             f"parsererror {n} v"),
        # The same parser behind XMLHttpRequest.responseXML, and in front of
        # it the code that turns a data: URL into bytes and bytes into text.
        page("xml: a deep document as the response of a request",
             get + f"var src = '<a>'.repeat({n}) + '{BOTTOM}' + '</a>'.repeat({n}); var x = get('data:text/xml,' + src), d = x.responseXML;" + depth,
             ["depth", "x.responseText === src", "d.documentElement.textContent", "x.status"], f"{n} true {BOTTOM} 200"),
        page("xml: a long response that is not well formed, and one that expands",
             get + laughs +
             f"var open = get('data:application/xml,' + '<a>'.repeat({n}));"
             "var bomb = get('data:application/xml,' + encodeURIComponent(dtd + ']><l>&j;</l>'));",
             ["open.responseXML", "open.responseText.length", "bomb.responseXML", "bomb.status"], f"null {3 * n} null 200"),
        page("xml: a long response in base64, and one with every byte escaped",
             get +
             f"var body = '<r>' + 'abcdefghij'.repeat({n}) + '</r>', packed = get('data:text/xml;base64,' + btoa(body));"
             f"var escaped = get('data:text/xml,%3Cr%3E' + '%C3%A9'.repeat({n}) + '%3C/r%3E');",
             ["packed.responseText === body", "packed.responseXML.documentElement.textContent.length",
              "escaped.responseText.length", "escaped.responseXML.documentElement.firstChild.length"],
             f"true {10 * n} {n + 7} {n}"),
        page("xml: a response in UTF-16, and one whose every byte is an error",
             get +
             f"var wide = get('data:text/xml;charset=utf-16le,' + '%3C%00r%00%3E%00' + 'x%00'.repeat({n}) + '%3C%00/%00r%00%3E%00');"
             f"var broken = get('data:text/plain,' + '%FF'.repeat({n}));",
             ["wide.responseXML.documentElement.textContent.length", "wide.responseText.length",
              "broken.responseText.length", "broken.responseText.charCodeAt(0)"],
             f"{n} {n + 7} {n} 65533"),
    ]


def scripted_pages(n: int) -> list[Page]:
    def page(name: str, build: str, values: list[str], expect: str) -> Page:
        return Page(name, document(script(build) + script(report(*values))), expect=expect)

    c = chain(n)
    custom = min(n, 20000)
    pages = [
        page("chain, dropped", c + "top = at = d = null;", ["'dropped'"], "dropped"),
        page("chain, textContent", c, ["top.textContent"], BOTTOM),
        page("chain, innerHTML and outerHTML", c, ["top.innerHTML.length", "top.outerHTML.length"],
             f"{(n - 1) * 11 + len(BOTTOM)} {n * 11 + len(BOTTOM)}"),
        page("chain, querySelectorAll", c, ["top.querySelectorAll('div').length", "top.querySelector('p')"], f"{n - 1} null"),
        page("chain, attached to the document", c + "document.body.appendChild(top);",
             ["document.querySelectorAll('div').length", "at.isConnected", "document.getElementById('nothing')"], f"{n} true null"),
        page("chain, attached and removed again", c + "document.body.appendChild(top); document.body.removeChild(top);",
             ["at.isConnected", "top.parentNode"], "false null"),
        page("chain, replaced through innerHTML",
             c + f"document.body.appendChild(top); top = at = d = null; document.body.innerHTML = '<p>{AFTER}</p>';",
             ["document.querySelectorAll('div').length"], "0"),
        page("chain, cleared through textContent", c + "document.body.appendChild(top); top.textContent = 'gone';",
             ["top.textContent", "top.childNodes.length"], "gone 1"),
        page("chain, an event from the bottom",
             c + "document.body.appendChild(top); var seen = 0;"
                 "top.addEventListener('ping', function () { seen++; });"
                 "document.addEventListener('ping', function () { seen++; });"
                 "at.dispatchEvent(typeof Event === 'function' ? new Event('ping', { bubbles: true }) : { type: 'ping', bubbles: true });",
             ["seen"], "2"),
        page("chain, the bottom moved to the top",
             c + "var moved = 0; try { top.appendChild(at); moved = 1; } catch (e) { moved = e.name; }",
             ["moved", "top.childNodes.length"], "1 2"),
        page("chain, the top appended to the bottom",
             c + "var name = 'no error'; try { at.appendChild(top); } catch (e) { name = e.name; }",
             ["name"], "HierarchyRequestError"),
        page("chain, walked up from the bottom", c + "var steps = 0; for (var p = at; p; p = p.parentNode) steps++;", ["steps"], f"{n}"),
        page("chain built from the bottom up",
             f"var top = document.createElement('div'); top.appendChild(document.createTextNode('{BOTTOM}'));"
             f"for (var i = 1; i < {n}; i++) {{ var d = document.createElement('div'); d.appendChild(top); top = d; }}",
             ["top.textContent", "top.querySelectorAll('div').length"], f"{BOTTOM} {n - 1}"),
        page("innerHTML set to a deep nest",
             f"var host = document.createElement('div'); host.innerHTML = '<div>'.repeat({n}) + '{BOTTOM}';"
             "document.body.appendChild(host);",
             ["host.querySelectorAll('div').length", "host.textContent"], f"{n} {BOTTOM}"),
        page("innerHTML read back and set again",
             f"var host = document.createElement('div'); host.innerHTML = '<span>'.repeat({n}) + '{BOTTOM}';"
             "var copy = document.createElement('div'); copy.innerHTML = host.innerHTML;",
             ["copy.querySelectorAll('span').length", "copy.textContent"], f"{n} {BOTTOM}"),
        page("innerHTML nested through script, level by level",
             f"var top = document.createElement('div'), at = top;"
             f"for (var i = 0; i < {min(n, 30000)}; i++) {{ at.innerHTML = '<div><div></div></div>'; at = at.querySelector('div').querySelector('div'); }}",
             ["top.querySelectorAll('div').length"], f"{2 * min(n, 30000)}"),
        page("shadow roots inside shadow roots",
             f"var top = document.createElement('div'), at = top;"
             f"for (var i = 0; i < {n}; i++) {{ var root = at.attachShadow({{ mode: 'open' }});"
             f" var d = document.createElement('div'); root.appendChild(d); at = d; }}"
             "document.body.appendChild(top);",
             ["at.isConnected", "top.shadowRoot.querySelectorAll('div').length"], "true 1"),
        page("shadow roots inside shadow roots, dropped",
             f"var top = document.createElement('div'), at = top;"
             f"for (var i = 0; i < {n}; i++) {{ var root = at.attachShadow({{ mode: 'open' }});"
             f" var d = document.createElement('div'); root.appendChild(d); at = d; }}"
             "top = at = root = d = null;",
             ["'dropped'"], "dropped"),
        page("shadow root filled through innerHTML",
             f"var host = document.createElement('div'); var root = host.attachShadow({{ mode: 'open' }});"
             f"root.innerHTML = '<div>'.repeat({n}) + '{BOTTOM}'; document.body.appendChild(host);",
             ["root.querySelectorAll('div').length", "root.innerHTML.length"], f"{n} {n * 11 + len(BOTTOM)}"),
        page("fragment appended level by level",
             f"var top = document.createElement('div'), at = top;"
             f"for (var i = 0; i < {n}; i++) {{ var f = document.createDocumentFragment();"
             f" var d = document.createElement('div'); f.appendChild(d); at.appendChild(f); at = d; }}",
             ["top.querySelectorAll('div').length"], f"{n}"),
        page("insertBefore chain",
             f"var top = document.createElement('div'), at = top;"
             f"for (var i = 0; i < {n}; i++) {{ var d = document.createElement('div');"
             f" var t = document.createTextNode(''); at.appendChild(t); at.insertBefore(d, t); at = d; }}",
             ["top.querySelectorAll('div').length", "top.textContent.length"], f"{n} 0"),
        page("custom elements, connected and disconnected",
             "var connected = 0, gone = 0;"
             "class Deep extends HTMLElement { connectedCallback() { connected++; } disconnectedCallback() { gone++; } }"
             "customElements.define('deep-el', Deep);"
             f"var top = document.createElement('deep-el'), at = top;"
             f"for (var i = 1; i < {custom}; i++) {{ var d = document.createElement('deep-el'); at.appendChild(d); at = d; }}"
             "document.body.appendChild(top); document.body.removeChild(top);",
             ["connected", "gone"], f"{custom} {custom}"),
        page("custom element that empties its parent when connected",
             "var calls = 0;"
             "class Eater extends HTMLElement { connectedCallback() { calls++; var p = this.parentNode;"
             " while (p && p.firstChild) p.removeChild(p.firstChild); } }"
             "customElements.define('eat-el', Eater);"
             "var top = document.createElement('div');"
             "for (var i = 0; i < 50; i++) { var e = document.createElement('eat-el'); e.appendChild(document.createElement('eat-el')); top.appendChild(e); }"
             "document.body.appendChild(top);",
             ["calls > 0", "top.childNodes.length"], "true 0"),
        page("canvas resized by script to more than memory",
             "var c = document.createElement('canvas'); document.body.appendChild(c);"
             "c.width = 100000; c.height = 100000; var ctx = c.getContext('2d'); ctx.fillRect(0, 0, 10, 10);"
             "var w = c.width; c.width = 20; c.height = 20; ctx.fillRect(0, 0, 10, 10);",
             ["w", "c.width", "c.height"], "100000 20 20"),
        page("canvases, more than memory holds",
             f"var kept = []; for (var i = 0; i < {min(n, 2000)}; i++) {{ var c = document.createElement('canvas');"
             " c.width = 2000; c.height = 2000; c.getContext('2d').fillRect(0, 0, 1, 1); kept.push(c); }",
             ["kept.length"], f"{min(n, 2000)}"),
        page("mutation observer on a deep subtree",
             c + "document.body.appendChild(top); var records = 0;"
                 "new MutationObserver(function (list) { records += list.length; }).observe(document.body, { childList: true, subtree: true });"
                 "at.appendChild(document.createElement('i'));",
             ["typeof records"], "number"),
    ]

    # Wide, not deep: one parent with very many children, made and taken
    # apart by script. The nodes parsed for `innerHTML` were taken off the
    # front of a list one at a time, twice over, and every later sibling
    # moved up each time: 100,000 elements in one string took 9 seconds.
    wide = min(n, 200000)
    pages += [
        page("innerHTML with many elements side by side",
             f"var host = document.createElement('div'); host.innerHTML = '<i>x</i>'.repeat({wide});",
             ["host.children.length", "host.innerHTML.length", "host.textContent.length"], f"{wide} {wide * 8} {wide}"),
        page("innerHTML with many elements, emptied and filled again",
             "var host = document.createElement('div'); document.body.appendChild(host);"
             f"host.innerHTML = '<i></i>'.repeat({wide}); var first = host.children.length; host.innerHTML = '';"
             f"var none = host.childNodes.length; host.innerHTML = '<b></b>'.repeat({wide}); host.textContent = '';",
             ["first", "none", "host.childNodes.length"], f"{wide} 0 0"),
        page("fragment with many children",
             "var f = document.createDocumentFragment();"
             f"for (var i = 0; i < {wide}; i++) f.appendChild(document.createElement('i'));"
             "var host = document.createElement('div'); document.body.appendChild(host); host.appendChild(f);",
             ["host.children.length"], f"{wide}"),
    ]

    # The same chain in the document, as each kind of box layout knows.
    kinds = {
        "blocks": ("div", ""),
        "inline boxes": ("span", ""),
        "flex containers": ("div", "display:flex"),
        "grid containers": ("div", "display:grid"),
        "floats": ("div", "float:left"),
        "inline-blocks": ("div", "display:inline-block"),
        "clipping boxes": ("div", "overflow:hidden"),
        "positioned boxes": ("div", "position:relative"),
        "absolutely positioned boxes": ("div", "position:absolute"),
        "table cells": ("div", "display:table-cell"),
        "list items": ("li", ""),
        "boxes with padding and border": ("div", "padding:1px;border:1px solid red"),
    }
    for name, (tag, style) in kinds.items():
        pages.append(page(f"laid out: chain of {name}", chain(n, tag, style) + "document.body.appendChild(top);",
                          ["at.isConnected"], "true"))
    pages.append(page("laid out: tables built by script",
                      f"var top = document.createElement('table'), at = top;"
                      f"for (var i = 0; i < {n}; i++) {{ var tr = document.createElement('tr'), td = document.createElement('td'),"
                      f" t = document.createElement('table'); at.appendChild(tr); tr.appendChild(td); td.appendChild(t); at = t; }}"
                      "document.body.appendChild(top);", ["at.isConnected"], "true"))
    pages.append(page("laid out: svg groups built by script",
                      f"var top = document.createElement('svg'), at = top;"
                      f"for (var i = 0; i < {n}; i++) {{ var g = document.createElement('g'); at.appendChild(g); at = g; }}"
                      "at.appendChild(document.createElement('rect')); document.body.appendChild(top);", ["at.isConnected"], "true"))
    pages.extend(xml_pages(n, page))
    return pages


# --- Random pages at the nesting limit -------------------------------------------------

NESTING = ("div div div div span p b i a table tbody tr td th caption colgroup ul li dl dd dt form button select "
           "optgroup option h1 h2 svg g foreignObject desc math mi mtext annotation-xml template nobr em font "
           "frameset noscript object marquee applet title textarea pre listing center section ruby rt rb").split()
VOID = "br img input hr col wbr meta link area".split()


def random_page(rng: random.Random, tokens: int) -> str:
    """Start tags, end tags, comments and text, after enough <div> to reach
    the nesting limit. Mostly start tags, so that most of the page is parsed
    at the limit."""
    out = ["<!doctype html>", "<div>" * (MAX_TREE_DEPTH - rng.randrange(0, 40))]
    favourites = rng.sample(NESTING, 6)
    for _ in range(tokens):
        roll = rng.random()
        tag = rng.choice(favourites) if rng.random() < 0.6 else rng.choice(NESTING)
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
            out.append(rng.choice(("</table>", "</template>", "</select>", "</svg>", "</math>", "</body>", "</html>", "<body>", "<html>")))
    out.append(f"<p>{AFTER}</p>")
    return "".join(out)


def deepest_element(dump: bytes) -> int:
    """Depth of the deepest element in an html5lib tree dump (<html> is 1).
    A template's contents are one pseudo-level ("content") deeper than the
    template; that level is not an element and is not counted."""
    deepest = 0
    content_levels: list[int] = []      # indentations of the open "content" lines
    for line in dump.split(b"\n"):
        if not line.startswith(b"| "):
            continue
        body = line[2:]
        indent = len(body) - len(body.lstrip(b" "))
        level = indent // 2
        while content_levels and content_levels[-1] >= level:
            content_levels.pop()
        text = body[indent:]
        if text == b"content":
            content_levels.append(level)
        elif text.startswith(b"<") and not text.startswith(b"<!"):
            deepest = max(deepest, level + 1 - len(content_levels))
    return deepest


# --- Running ----------------------------------------------------------------------

class Outcome:
    def __init__(self) -> None:
        self.status = "ok"                  # ok | signal NAME | exit N | timeout | memory | output
        self.seconds = 0.0
        self.peak_mb = 0.0
        self.out_bytes = 0
        self.head = b""                     # the start of the output: what scripts print comes first
        self.seen: set[bytes] = set()       # which of the markers occur anywhere in the output
        self.stderr_tail = b""
        self.deepest = 0                    # deepest element, when asked to measure an html5lib dump


def kill(process: subprocess.Popen) -> None:
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except OSError:
        pass


def run(argv: list[str], *, timeout: float, memory_mb: int, output_mb: int, stack_mb: int,
        markers: tuple[bytes, ...], measure_depth: bool = False) -> Outcome:
    """Runs one process. Its output is searched for `markers` as it arrives and
    thrown away, except for the start: the dump of a deep tree is large."""
    outcome = Outcome()
    started = time.time()

    def limit_stack() -> None:
        _, hard = resource.getrlimit(resource.RLIMIT_STACK)
        soft = stack_mb << 20
        if hard != resource.RLIM_INFINITY:
            soft = min(soft, hard)
        resource.setrlimit(resource.RLIMIT_STACK, (soft, hard))

    env = dict(os.environ)
    env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
    process = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True,
                               preexec_fn=limit_stack, env=env)
    killed_for: list[str] = []

    def drain(stream, is_stdout: bool) -> None:
        head, tail, total = b"", b"", 0
        kept: list[bytes] = []
        overlap = max((len(m) for m in markers), default=1) - 1
        while True:
            chunk = stream.read(1 << 16)
            if not chunk:
                break
            total += len(chunk)
            if len(head) < 1 << 18:
                head += chunk[: (1 << 18) - len(head)]
            window = tail[-overlap:] + chunk if overlap and is_stdout else chunk
            if is_stdout:
                for marker in markers:
                    if marker not in outcome.seen and marker in window:
                        outcome.seen.add(marker)
                if measure_depth:
                    kept.append(chunk)
            tail = window[-4000:]
            if is_stdout and total > output_mb << 20 and not killed_for:
                killed_for.append("output")
                kill(process)
        if is_stdout:
            outcome.out_bytes, outcome.head = total, head
            if measure_depth:
                outcome.deepest = deepest_element(b"".join(kept))
        else:
            outcome.stderr_tail = tail

    readers = [threading.Thread(target=drain, args=(process.stdout, True), daemon=True),
               threading.Thread(target=drain, args=(process.stderr, False), daemon=True)]
    for reader in readers:
        reader.start()
    page_kb = os.sysconf("SC_PAGE_SIZE") / 1024.0
    while process.poll() is None:
        if time.time() - started > timeout and not killed_for:
            killed_for.append("timeout")
            kill(process)
        try:
            with open(f"/proc/{process.pid}/statm") as statm:
                resident_mb = int(statm.read().split()[1]) * page_kb / 1024.0
            outcome.peak_mb = max(outcome.peak_mb, resident_mb)
            if resident_mb > memory_mb and not killed_for:
                killed_for.append("memory")
                kill(process)
        except (OSError, ValueError, IndexError):
            pass
        time.sleep(0.02)
    for reader in readers:
        reader.join()
    outcome.seconds = time.time() - started
    code = process.returncode
    if killed_for:
        outcome.status = killed_for[0]
    elif code < 0:
        try:
            outcome.status = f"signal {signal.Signals(-code).name}"
        except ValueError:
            outcome.status = f"signal {-code}"
    elif code != 0:
        outcome.status = f"exit {code}"
    return outcome


def judge(page: Page, kind: str, outcome: Outcome, layout_check: bool) -> str:
    """What is wrong with this run; empty when nothing is."""
    if outcome.status != "ok":
        return outcome.status
    seen = outcome.seen

    def has(pattern: str, text: str) -> bool:
        return (pattern % text).encode() in seen

    if page.expect is not None and kind != "html5lib":
        printed = [line for line in outcome.head.split(b"\n") if line.startswith(SCRIPT_OK.encode())]
        if not printed:
            return "the last script printed nothing"
        if printed[-1].decode("utf-8", "replace") != f"{SCRIPT_OK} {page.expect}":
            return f"the last script printed {printed[-1][len(SCRIPT_OK) + 1:][:60]!r}, expected {page.expect[:60]!r}"
    if page.max_depth is not None and kind == "html5lib" and outcome.deepest > page.max_depth:
        return f"an element is {outcome.deepest} deep; the parser's limit is {page.max_depth}"
    if kind == "html5lib":
        if page.after and AFTER.encode() not in seen:
            return "the text after the nest is not in the tree"
        if page.bottom and BOTTOM.encode() not in seen:
            return "the text at the bottom of the nest is not in the tree"
    if kind == "dumps":
        for wanted, text, what in ((page.after, AFTER, "after the nest"), (page.bottom, BOTTOM, "at the bottom of the nest")):
            if not wanted:
                continue
            if not has(IN_TREE, text):
                return f"the text {what} is not in the tree"
            if not layout_check and (text == BOTTOM or page.after_is_deep):
                continue
            if not has(LAID_OUT, text):
                return f"the text {what} was not laid out"
            if not has(PAINTED, text):
                return f"the text {what} was not painted"
    return ""


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=DEFAULT_BINARY)
    parser.add_argument("--depth", type=int, default=200_000, help="levels of nesting (default 200000)")
    parser.add_argument("--only", default="", help="only pages whose name contains this text")
    parser.add_argument("--runs", default="", help="comma-separated kinds of run (dumps, terminal, html5lib); default: all")
    parser.add_argument("--random", type=int, default=0, metavar="N", help="also run N random pages at the nesting limit")
    parser.add_argument("--seed", type=int, default=20261110, help="seed of the random pages")
    parser.add_argument("--random-only", action="store_true", help="run only the random pages")
    parser.add_argument("--stack-mb", type=int, default=8, help="stack size each run gets (default 8, the usual default)")
    parser.add_argument("--no-layout-check", action="store_true",
                        help="do not require the innermost text to be laid out and painted "
                             "(a sanitizer build or a small stack stops layout earlier)")
    parser.add_argument("--timeout", type=float, default=60.0, help="seconds per run (default 60)")
    parser.add_argument("--memory-mb", type=int, default=2048, help="resident memory at which a run is ended (default 2048)")
    parser.add_argument("--output-mb", type=int, default=4096, help="output at which a run is ended (default 4096)")
    parser.add_argument("--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 1))), help="runs at a time")
    parser.add_argument("--keep", default="", help="write the pages to this directory and leave them there")
    parser.add_argument("--list", action="store_true", help="print the page names and exit")
    parser.add_argument("--quiet", action="store_true", help="print failures and the summary only")
    args = parser.parse_args()

    pages: list[Page] = [] if args.random_only else parsed_pages(args.depth) + scripted_pages(args.depth)
    if args.only:
        pages = [page for page in pages if args.only in page.name]
    rng = random.Random(args.seed)
    for index in range(args.random):
        # No element may be deeper than the limit, whatever road it took into
        # the tree: inserted, reconstructed, foster-parented, or moved by the
        # adoption agency algorithm together with everything inside it.
        pages.append(Page(f"random page {index} (seed {args.seed})", random_page(rng, rng.randrange(500, 6000)),
                          after=False, max_depth=MAX_TREE_DEPTH))
    if args.list:
        for page in pages:
            print(page.name)
        return 0
    if not Path(args.binary).exists():
        print(f"error: {args.binary} not found (run `make`)", file=sys.stderr)
        return 2
    kinds = [kind for kind in (args.runs.split(",") if args.runs else RUNS) if kind]
    for kind in kinds:
        if kind not in RUNS:
            print(f"error: no such kind of run: {kind} (there are: {', '.join(RUNS)})", file=sys.stderr)
            return 2

    workdir = Path(args.keep) if args.keep else Path(tempfile.mkdtemp(prefix="litebird-deep-"))
    workdir.mkdir(parents=True, exist_ok=True)
    markers = tuple((pattern % text).encode() for pattern in (IN_TREE, LAID_OUT, PAINTED) for text in (BOTTOM, AFTER))
    markers += (BOTTOM.encode(), AFTER.encode())
    failures: list[str] = []
    slowest = (0.0, "")
    total_runs = 0
    print_lock = threading.Lock()

    def one(job: tuple[int, Page, str]) -> tuple[str, float]:
        index, page, kind = job
        path = workdir / f"page{index:04d}.html"
        if not path.exists():
            path.write_text(page.html, encoding="utf-8")
        argv = [args.binary] + [part.replace("{bmp}", str(workdir / f"page{index:04d}.bmp")) for part in RUNS[kind]] + [str(path)]
        outcome = run(argv, timeout=args.timeout, memory_mb=args.memory_mb, output_mb=args.output_mb,
                      stack_mb=args.stack_mb, markers=markers,
                      measure_depth=kind == "html5lib" and page.max_depth is not None)
        problem = judge(page, kind, outcome, not args.no_layout_check)
        line = (f"{'FAIL' if problem else 'ok  '} {page.name:52.52} {kind:9} {outcome.seconds:6.1f}s "
                f"{outcome.peak_mb:6.0f}MB {outcome.out_bytes / 1e6:8.1f}MB out  {problem}")
        if problem:
            detail = outcome.stderr_tail.decode("utf-8", "replace").strip().split("\n")
            summary = next((l for l in detail if "ERROR: " in l or "SUMMARY: " in l or "runtime error" in l), "")
            if summary:
                line += f"\n       {summary[:220]}"
        if problem or not args.quiet:
            with print_lock:
                print(line, flush=True)
        try:
            os.remove(workdir / f"page{index:04d}.bmp")
        except OSError:
            pass
        return (f"{page.name} [{kind}]: {problem}" if problem else "", outcome.seconds)

    try:
        jobs = [(index, page, kind) for index, page in enumerate(pages) for kind in kinds
                if not (kind == "html5lib" and page.has_script)]
        for index, page in enumerate(pages):
            (workdir / f"page{index:04d}.html").write_text(page.html, encoding="utf-8")
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            for job, (failure, seconds) in zip(jobs, pool.map(one, jobs)):
                total_runs += 1
                if failure:
                    failures.append(failure)
                if seconds > slowest[0]:
                    slowest = (seconds, f"{job[1].name} [{job[2]}]")
    finally:
        if not args.keep:
            shutil.rmtree(workdir, ignore_errors=True)

    print(f"deep pages: {total_runs} runs over {len(pages)} pages at depth {args.depth} on a {args.stack_mb} MB stack, "
          f"{len(failures)} failed; slowest {slowest[0]:.1f} s ({slowest[1]})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
