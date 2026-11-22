#!/usr/bin/env python3
"""How a suite in tests/test_suite.cpp begins, and whether a merge can resolve it.

    python3 tools/ci/check_suites.py            # the count, and any suite that would merge badly
    python3 tools/ci/check_suites.py --list     # every suite that still writes the boilerplate
    python3 tools/ci/check_suites.py --self-test  # how a suite is read, on fixed texts; no network

Why this exists. A suite that builds its own page and interpreter writes six
lines that are the same in every such suite:

    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.load_page("<!DOCTYPE html><body></body>");
    js.notify_document_loaded();

Two branches that each add a suite write those six lines identically, and git
sees them as common context between two differences. It does not merge two new
functions that happen to share a prologue: it produces **two** conflict regions,
one around each `TEST(...) {` and one around each body, with the shared prologue
left outside both. Taking "ours" and taking "theirs" then each yield a function
whose body belongs to the other branch, and the file still compiles.

That happened five times merging one branch into this one, and the resolution
was, every time, to take both functions whole from their own side of the merge
and write them back in order. It is the shape of the boilerplate that causes it,
so `TestPage` (see tests/test_suite.cpp) puts it behind one call. Two suites
written through it share nothing but `TEST` and the closing brace, and the same
merge is one conflict that encloses the whole function: keeping both is the
whole resolution.

This tool reports the suites that still write the six lines, because a suite
added that way will merge badly again. It does not fix them: a suite that builds
a tree between the document and the interpreter needs `TestPage::Built`, and
converting one is a judgement about what its prologue is for. The count is
reported so it can be watched going down, and so a new one is noticed.

A suite in a file of its own (`tests/*_suite.cpp`) would have no prologue to
share with anything and this would not apply; every suite is in the one file.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SUITE_FILE = ROOT / "tests" / "test_suite.cpp"

# The prologue, as lines that may be indented by any amount. `Document` and
# `JsInterpreter` by their unqualified names, because that is how the suites
# write them inside `using namespace litebird`.
BOILERPLATE = (
    "auto doc = std::make_shared<Document>();",
    "JsInterpreter js(doc);",
)
# Any of these as well, and the suite is one that builds its own tree before the
# interpreter exists.
BETWEEN = ("set_url(", "append_child(", "set_attribute(", "load_page(")

SUITE_START = re.compile(r"(?m)^TEST\((\w+)\)\s*\{")


def suites(text: str) -> list[tuple[str, str]]:
    """Every `TEST(name)` in the file and the text of its function."""
    starts = [(m.group(1), m.start()) for m in SUITE_START.finditer(text)]
    out = []
    for i, (name, at) in enumerate(starts):
        end = starts[i + 1][1] if i + 1 < len(starts) else len(text)
        out.append((name, text[at:end]))
    return out


def writes_its_own_prologue(body: str) -> bool:
    """Whether this suite writes the two lines every suite of the kind shares.

    Both, not either: a suite with a document and no interpreter is not the
    shape two branches collide on, and neither is one with an interpreter over a
    document it did not make.
    """
    return all(line in body for line in BOILERPLATE)


def through_the_helper(body: str) -> bool:
    """Whether the suite takes its page and interpreter from `TestPage`."""
    return "TestPage page" in body


# The page `TestPage` makes: its URL and nothing else set.
HELPER_URL = "https://example.test/"


def needs_its_own_page(body: str) -> bool:
    """Whether this suite sets a document up in a way `TestPage` cannot give it.

    `TestPage` makes one page: an empty body at `https://example.test/`. A suite
    that wants any other URL, or a tree before the interpreter exists, has to say
    so — `TestPage::WithUrl` takes the URL, `TestPage::Built` runs a builder — and
    one that quietly keeps its own `set_url` while claiming to be converted is
    the mistake this looks for. It is not hypothetical: converting nine suites
    whose scripts read `location.origin` and giving them all the helper's URL
    broke eight suites that no compiler complained about, because the scripts
    compared the URL to a string and the assertion failed at run time.
    """
    urls = re.findall(r'set_url\("([^"]*)"\)', body)
    return any(url != HELPER_URL for url in urls)


def is_script_suite(body: str) -> bool:
    """Whether this suite runs script at all: it makes an interpreter one way or another."""
    return "JsInterpreter" in body


def report(list_only: bool) -> int:
    if not SUITE_FILE.is_file():
        print(f"check_suites: {SUITE_FILE} is not here")
        return 1
    text = SUITE_FILE.read_text()
    found = suites(text)
    script = [(n, b) for n, b in found if is_script_suite(b)]
    by_helper = [(n, b) for n, b in script if through_the_helper(b)]
    inline = [(n, b) for n, b in script if not through_the_helper(b)]

    if list_only:
        for name, body in inline:
            between = [k for k in BETWEEN if k in body.split("JsInterpreter js(doc);")[0]]
            print(f"  {name}" + (f"  (builds a tree: {', '.join(between)})" if between else ""))
        return 0

    print(f"  ok    {len(found)} suites, {len(script)} of them run script: "
          f"{len(by_helper)} through TestPage, {len(inline)} writing their own prologue")
    # A converted suite that still sets its own URL is using the helper's page
    # for a document that is not the helper's page.
    wrong = [(n, b) for n, b in by_helper if needs_its_own_page(b)]
    if wrong:
        for name, body in wrong:
            urls = re.findall(r'set_url\("([^"]*)"\)', body)
            print(f"  NO    {name} uses TestPage and sets its own URL ({', '.join(urls)}): "
                  f"pass it to TestPage::WithUrl, or keep it inline")
        return 1
    if inline:
        # A note, not a failure. Converting one means reading what its prologue
        # is for, and the ones that build a tree need `TestPage::Built`; the
        # number is what matters, and it is falling.
        print(f"  note  {len(inline)} suite(s) would merge badly with another new one "
              f"(`python3 tools/ci/check_suites.py --list`)")
    return 0


def self_test() -> int:
    """How a suite is read, on fixed texts. Needs no network and no build."""
    inline = """TEST(mine) {
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    JsInterpreter js(doc);
    js.eval("1");
}
"""
    helper = """TEST(mine) {
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval("1");
}
"""
    built = """TEST(mine) {
    TestPage page(TestPage::Built([](Document& d) { d.append_child(body); }));
    JsInterpreter& js = page.js;
}
"""
    other_url = """TEST(mine) {
    TestPage page(TestPage::WithUrl{});
    JsInterpreter& js = page.js;
    doc->set_url("https://other.example.test/page.html");
}
"""
    doc_only = """TEST(mine) {
    auto doc = std::make_shared<Document>();
    doc->set_url("https://example.test/");
    js.load_page("<p>");
}
"""
    cases = [
        ("a suite that writes the shared prologue is one that merges badly",
         (writes_its_own_prologue(inline), through_the_helper(inline)), (True, False)),
        ("a suite behind TestPage shares nothing with another and merges cleanly",
         (writes_its_own_prologue(helper), through_the_helper(helper)), (False, True)),
        ("a suite that builds a tree through Built is read as one too",
         (writes_its_own_prologue(built), through_the_helper(built)), (False, True)),
        ("a document with no interpreter is not the shape two branches collide on",
         writes_its_own_prologue(doc_only), False),
        ("...and is not a script suite, so it is neither", is_script_suite(doc_only), False),
        ("a suite on the helper's page needs nothing more", needs_its_own_page(helper), False),
        ("a suite that sets another URL needs its own page", needs_its_own_page(other_url), True),
        ("...and so does one that builds a tree", needs_its_own_page(built), False),
        ("two suites are found, and each is its own text", len(suites(inline + helper)), 2),
        ("the name is the name in TEST(...)", [n for n, _b in suites(inline + helper)], ["mine", "mine"]),
    ]
    failed = 0
    for name, got, wanted in cases:
        ok = got == wanted
        failed += not ok
        print(f"  [{'ok  ' if ok else 'FAIL'}] {name}")
        if not ok:
            print(f"         wanted {wanted!r}")
            print(f"         got    {got!r}")
    print(f"\n{len(cases) - failed}/{len(cases)} pass")
    return 1 if failed else 0


def main() -> int:
    arguments = sys.argv[1:]
    if arguments == ["--self-test"]:
        return self_test()
    if arguments == ["--list"]:
        return report(list_only=True)
    if arguments:
        sys.exit(__doc__)
    return report(list_only=False)


if __name__ == "__main__":
    sys.exit(main())
