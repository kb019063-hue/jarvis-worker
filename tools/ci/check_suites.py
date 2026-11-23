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

A suite in a file of its own has no prologue to share with anything, and none
of this applies to it. Since 23 November 2026 that is where a new suite goes:
one file in tests/suites/, read at the end of tests/test_suite.cpp inside a
namespace named after the file (tests/suites/README.md says why: 44 of the 285
merges of `main` into a branch were in conflict in the one file). What this
tool holds such a file to is what makes that work:

  - its name is a number and words, `218-text-decoration-lines.cpp`;
  - it has one `TEST(name)`, at the start of a line: the suite count is the
    number of files, and tools/ci/check_pr.py counts names without reading;
  - it has no `#include`: it would be read inside the file's namespace, and a
    header that had not been read before would declare `std` in there;
  - it has no `RUN_TEST` and no `main`;
  - no two suites, in the files or in tests/test_suite.cpp, have one name.

And of tests/test_suite.cpp it now says what nothing did: a `TEST(name)` there
with no `RUN_TEST(name)` line is a suite that is never run. (`make test` ends
with status 1 for it too; this says it without a build.)
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SUITE_FILE = ROOT / "tests" / "test_suite.cpp"
SUITE_DIR = ROOT / "tests" / "suites"

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
OWN_FILE_NAME = re.compile(r"\d+(-[a-z0-9]+)+\.cpp")
LISTED = re.compile(r"(?m)^    RUN_TEST\((\w+)\)")


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


def own_file_problems(name: str, text: str) -> list[str]:
    """What is wrong with one file of tests/suites/, by its name and its text."""
    problems = []
    if not OWN_FILE_NAME.fullmatch(name):
        problems.append(f"tests/suites/{name}: the name is a number and words in small letters, "
                        "`218-text-decoration-lines.cpp`")
    names = [n for n, _body in suites(text)]
    if len(names) != 1:
        problems.append(f"tests/suites/{name}: {len(names)} `TEST(name) {{` at the start of a line; "
                        "a file is one suite, and two suites are two files")
    if re.search(r"(?m)^\s*#\s*include\b", text):
        problems.append(f"tests/suites/{name}: an `#include`. The file is read inside a namespace, at the end of "
                        "tests/test_suite.cpp; a header it needs goes to the list at the top of that file")
    if re.search(r"(?m)^\s*RUN_TEST\s*\(", text) or re.search(r"(?m)^int\s+main\s*\(", text):
        problems.append(f"tests/suites/{name}: a `RUN_TEST` or a `main`. A file here is run without either")
    return problems


def never_run(text: str) -> list[str]:
    """The `TEST(name)` of tests/test_suite.cpp that `main()` has no `RUN_TEST(name)` line for."""
    listed = set(LISTED.findall(text))
    return [name for name, _body in suites(text) if name not in listed]


def twice(names: list[str]) -> list[str]:
    """The names more than one suite has."""
    seen: set[str] = set()
    out = []
    for name in names:
        if name in seen and name not in out:
            out.append(name)
        seen.add(name)
    return out


def report(list_only: bool) -> int:
    if not SUITE_FILE.is_file():
        print(f"check_suites: {SUITE_FILE} is not here")
        return 1
    text = SUITE_FILE.read_text()
    found = suites(text)
    own = sorted(SUITE_DIR.glob("*.cpp")) if SUITE_DIR.is_dir() else []
    problems = [f"tests/test_suite.cpp: TEST({name}) has no RUN_TEST({name}) line in main(): it is never run"
                for name in never_run(text)]
    own_names = []
    for path in own:
        own_text = path.read_text()
        problems += own_file_problems(path.name, own_text)
        own_names += [n for n, _body in suites(own_text)]
    problems += [f"two suites are called {name}" for name in twice([n for n, _b in found] + own_names)]
    if not list_only:
        if problems:
            print(f"  NO    {len(problems)} thing(s) wrong with where the suites are (tests/suites/README.md):")
            for problem in problems:
                print(f"        {problem}")
            return 1
        print(f"  ok    {len(own)} suite(s) are files of their own in tests/suites/, each in the form of its README")
    script = [(n, b) for n, b in found if is_script_suite(b)]
    by_helper = [(n, b) for n, b in script if through_the_helper(b)]
    inline = [(n, b) for n, b in script if not through_the_helper(b)]

    if list_only:
        for name, body in inline:
            between = [k for k in BETWEEN if k in body.split("JsInterpreter js(doc);")[0]]
            print(f"  {name}" + (f"  (builds a tree: {', '.join(between)})" if between else ""))
        return 0

    print(f"  ok    {len(found)} suites in tests/test_suite.cpp, {len(script)} of them run script: "
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
        # A suite that is a file of its own (tests/suites/).
        ("a file with one TEST, a helper beside it and a comment above is in form",
         own_file_problems("218-text-decoration-lines.cpp",
                           "// Phase 218.\nstatic int f() { return 1; }\n" + helper), []),
        ("a TEST whose brace is on the next line is one suite too",
         own_file_problems("218-a.cpp", "TEST(mine)\n{\n}\n"), []),
        ("two suites in one file are two files", len(own_file_problems("218-a.cpp", inline + helper)), 1),
        ("a file with no TEST is no suite", len(own_file_problems("218-a.cpp", "static void test_mine() {}\n")), 1),
        ("an indented TEST is not at the start of a line, and is not counted",
         len(own_file_problems("218-a.cpp", "namespace x {\n    TEST(mine) {\n    }\n}\n")), 1),
        ("an #include is refused, indented or not",
         [len(own_file_problems("218-a.cpp", lead + "#include <map>\n" + helper)) for lead in ("", "  ")], [1, 1]),
        ("...and a line that only says the word is not one",
         own_file_problems("218-a.cpp", "// no #include here\n" + helper), []),
        ("a RUN_TEST in a file is refused, and a comment that names one is not",
         [len(own_file_problems("218-a.cpp", helper + line)) for line in
          ("    RUN_TEST(mine);\n", "// no RUN_TEST(mine) line\n", "int main() {\n}\n")], [1, 0, 1]),
        ("the name of the file is a number and words",
         [len(own_file_problems(name, helper)) for name in
          ("218-a.cpp", "000-a-suite-in-a-file-of-its-own.cpp", "a.cpp", "218_a.cpp", "218-A.cpp", "218.cpp")],
         [0, 0, 1, 1, 1, 1]),
        ("a TEST of the big file without its RUN_TEST line is never run",
         never_run("TEST(one) {\n}\nTEST(two) {\n}\nint main() {\n    RUN_TEST(one);\n    // RUN_TEST(two);\n}\n"),
         ["two"]),
        ("...and with it, it is", never_run("TEST(one) {\n}\nint main() {\n    RUN_TEST(one);\n}\n"), []),
        ("a name two suites have is found once", twice(["a", "b", "a", "c", "a", "b"]), ["a", "b"]),
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
