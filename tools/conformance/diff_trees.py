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
    python3 tools/conformance/diff_trees.py --baseline /tmp/old/build/litebird   # exit 1 if a page got worse
    python3 tools/conformance/diff_trees.py --max-differ 15     # exit 1 if more than 15 pages differ
    python3 tools/conformance/diff_trees.py --check-reference   # parse5 itself against the corpus

`make diff-trees` runs it with a ceiling. It needs `node`, and the first time
`npm` and the network: parse5 is installed into build/parse5. For that reason
it is not part of CI.

What it does not tell:

- `<select>`, `<optgroup>` and `<option>` are left out unless --with-select is
  given. The standard changed how the content of a `<select>` is parsed, the
  corpus here follows the change, and parse5 8.1 does not: run
  --check-reference to see that those are the only cases of the corpus on
  which parse5 gives another tree than the expected one. With --with-select
  about one page in ten differs for that reason alone.
- The pages are whole documents with scripting enabled. Fragments,
  `--scripting=off`, character references, attributes beyond `a=1`, and the
  tokenizer states for scripts and comments are not exercised; the corpus is
  what covers those.
"""
import argparse
import difflib
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

PARSE5_VERSION = "8.1.1"
HERE = os.path.dirname(os.path.abspath(__file__))

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

    def tree(self, path: str) -> str:
        self.process.stdin.write(path + "\n")
        self.process.stdin.flush()
        if self.process.stdout.readline().strip() != "done":
            raise RuntimeError(f"parse5 gave no tree for {path}")
        with open(path + ".tree", encoding="utf-8", errors="surrogateescape", newline="") as handle:
            return handle.read()

    def close(self) -> None:
        self.process.stdin.close()
        self.process.wait()


def litebird_tree(binary: str, path: str) -> str:
    """The tree, or a line saying how the process ended when it printed none."""
    try:
        done = subprocess.run([binary, "--dump-dom-webplatform", path], stdout=subprocess.PIPE,
                              stderr=subprocess.DEVNULL, timeout=60)
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
            if case.fragment_context is not None or case.scripting is False:
                continue  # parse5_tree.mjs parses documents with scripting enabled
            path = os.path.join(scratch, f"case{total}.html")
            with open(path, "w", encoding="utf-8", errors="surrogateescape", newline="") as out:
                out.write(case.data)
            total += 1
            if reference.tree(path).rstrip("\n") != case.document.rstrip("\n"):
                unexpected.append((f"{dat.name} case {case.index}", case.data))
    if total == 0:
        print(f"no corpus in {DEFAULT_CORPUS}; run this from the top of the repository", file=sys.stderr)
        return 2
    with_select = [name for name, data in unexpected if "<select" in data.lower()]
    others = [name for name, data in unexpected if "<select" not in data.lower()]
    print(f"parse5 {version} against the corpus: {total - len(unexpected)} of {total} document cases give the "
          f"expected tree; of the other {len(unexpected)}, {len(with_select)} contain <select>")
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

    tags = args.tags.split() or TAGS
    if args.with_select:
        tags = tags + SELECT_TAGS
    if len(tags) < 6:
        print("--tags needs at least six names: six are drawn first for each page", file=sys.stderr)
        return 2
    stray = STRAY + ["</select>"] if args.with_select else STRAY
    rng = random.Random(args.seed)
    reference = Reference(args.parse5_dir)
    try:
        with tempfile.TemporaryDirectory() as scratch:
            if args.check_reference:
                return check_reference(reference, version, scratch)

            def trees_of(name: str, source: str) -> tuple[str, list[str]]:
                path = os.path.join(scratch, name)
                with open(path, "w", encoding="ascii", newline="") as out:
                    out.write(source)
                return reference.tree(path), [litebird_tree(binary, path) for binary in binaries]

            differing, lost, gained, baseline_equal = [], [], [], 0
            for number in range(args.pages):
                source = random_page(rng, tags, stray, args.extras)
                expected, actual = trees_of(f"page{number}.html", source)
                equal = actual[0] == expected
                if not equal:
                    differing.append((number, source, expected, actual[0]))
                if args.baseline:
                    was_equal = actual[1] == expected
                    baseline_equal += was_equal
                    if was_equal and not equal:
                        lost.append((number, source))
                    elif equal and not was_equal:
                        gained.append((number, source))

            what = "with <select>" if args.with_select else "no <select>"
            print(f"{args.pages} random pages (seed {args.seed}, 3 to 39 tokens each, {what}): "
                  f"{args.pages - len(differing)} trees equal to parse5 {version}'s, {len(differing)} differ")
            if args.baseline:
                print(f"{args.baseline}: {baseline_equal} equal; {len(gained)} pages equal now and not with it, "
                      f"{len(lost)} the other way")
                for number, source in lost[:max(args.show, 0)]:
                    print(f"  lost: page {number}: {source!r}")

            for number, source, expected, actual in differing[:max(args.show, 0)]:
                print(f"\npage {number}: {source!r}")
                changes = [line for line in difflib.unified_diff(expected.split("\n"), actual.split("\n"),
                                                                 "parse5", "litebird", lineterm="", n=1)]
                for line in changes[:14]:
                    print(f"    {line}")
                if len(changes) > 14:
                    print(f"    ... {len(changes) - 14} more lines")

            if args.reduce and differing:
                def differs(source: str) -> bool:
                    expected, actual = trees_of("reduced.html", source)
                    return actual[0] != expected

                reduced: dict[str, list[int]] = {}
                for number, source, _, _ in differing:
                    reduced.setdefault(reduce_page(source, differs), []).append(number)
                print(f"\nreduced, {len(reduced)} distinct:")
                for source, numbers in sorted(reduced.items(), key=lambda item: (len(item[0]), item[0])):
                    pages = "page " + str(numbers[0]) if len(numbers) == 1 else "pages " + ", ".join(map(str, numbers))
                    print(f"  {source!r}  ({pages})")
    finally:
        reference.close()

    if lost:
        print(f"\n{len(lost)} pages equalled parse5 with {args.baseline} and no longer do", file=sys.stderr)
        return 1
    if args.max_differ >= 0 and len(differing) > args.max_differ:
        print(f"\nmore than {args.max_differ} pages differ", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
