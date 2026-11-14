# XML conformance

The scoreboard of the XML parser is the W3C XML Conformance Test Suite of
23 September 2013 (<https://www.w3.org/XML/Test/>).

```bash
make xml-conformance                                           # the scoreboard
python3 tools/conformance/run_xml_conformance.py --show 20     # with the failures
python3 tools/conformance/run_xml_conformance.py --reference   # Expat and libxml2 on the same tests
```

## The suite is downloaded, not kept here

The runner fetches `xmlts20130923.zip` once into `build/xml-conformance/`
and unpacks it there. It asks the W3C first
(`https://www.w3.org/XML/Test/xmlts20130923.zip`) and, when that cannot be
reached, a public repository that holds the same file
(`kornelski/xml-rs`, `tests/`). Wherever the bytes come from, they are used
only if they are these:

```
sha256  f9510b3532926e1b4c2e54855b021e4b8a66ec98a5337dcf4ff07e8a41968deb
size    1,574,648 bytes; 3,386 files, 8.1 MB unpacked
```

A machine with no network needs the file put at
`build/xml-conformance/xmlts20130923.zip` by hand (or `--suite PATH`);
without it the runner stops and says so, and `make xml-conformance` and
`make asan` fail. They do not pass with the suite not run.

**Why it is not in the repository.** The suite is a collection of
collections (Sun, OASIS/NIST, IBM, Fuji Xerox, the University of Edinburgh,
James Clark's XMLTEST) and the archive carries no licence for the whole.
The one notice in it that speaks of redistribution, XMLTEST's
(`xmlconf/xmltest/readme.html`), allows passing on its own original archive
unchanged and ends: "Note that permission to distribute the collection in
any other form is not granted." The W3C publishes the suite; this
repository does not copy it. The two parsers Litebird is compared with do
the same: Expat's test script names the W3C's address
(`expat/tests/xmltest.sh`), and libxml2's CI image downloads the archive
(`.gitlab-ci/Dockerfile`).

## What is counted

The suite has 2,585 documents. A browser's XML parser does not validate,
knows namespaces, reads no external entity, and follows XML 1.0 Fifth
Edition, so:

| | Documents |
| :--- | ---: |
| **Counted**: must be accepted (`valid`, `invalid`) or rejected (`not-wf`); 261 of them also have a canonical form that must match | 1,702 |
| An edition before the fifth, where the fifth changed the answer (which characters a name may hold) | 310 |
| XML 1.1 and Namespaces 1.1 | 274 |
| The answer needs an external entity to be read | 247 |
| `error`: a parser may report it or not | 27 |
| An encoding this build cannot read (EUC-JP, Shift_JIS, ISO-2022-JP and the like) | 16 |
| Not namespace-well-formed on purpose (`NAMESPACE="no"`) | 9 |

Every one of the 2,585 is run, counted or not: none may crash the parser or
run out of time.
