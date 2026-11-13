# URL conformance corpus

The URL tests of the [web-platform-tests](https://github.com/web-platform-tests/wpt)
project, which is what browsers test their URL parsers with:

| File | From `url/resources/` at commit | What it holds |
| :--- | :--- | :--- |
| `urltestdata.json` | `065d99ab62bf0b6ca04907f423625801c350edb2` (2026-11-12) | An input, a base and either every part of the URL they make or `"failure": true` |
| `setters_tests.json` | `dca32277cbf3d40b3ec20bba999a987b0ea51ed7` (2026-11-13) | A URL, a part, a value assigned to it and the parts expected afterwards |

The files are copied unchanged; `LICENSE.md` is the project's licence
(3-Clause BSD).

## Running it

```
make url-conformance
python3 tools/conformance/run_url_conformance.py --show 20
```

The runner writes one script that holds both files, runs it with
`litebird --run-script` and reads the result. A parsing case passes when
`new URL(input, base)` gives every expected part (`href`, `protocol`,
`username`, `password`, `host`, `hostname`, `port`, `pathname`, `search`,
`hash`, and `origin` and `searchParams` when the case names them), or throws a
`TypeError` and `URL.canParse` says no when the case expects a failure. A
setter case passes when assigning the value leaves every expected part.

## What fails

Every case that fails is about a host name with characters outside ASCII
(`http://faß.example`, `http://Ｇｏ.com`, a soft hyphen in a host). Browsers
convert such a name to its ASCII form by Unicode's UTS #46 (case folding,
normalisation, validity checks, then Punycode: `xn--fa-hia.example`).
Litebird does not have that table: it keeps the name as written. See
`include/litebird/net/UrlParser.hpp`.
