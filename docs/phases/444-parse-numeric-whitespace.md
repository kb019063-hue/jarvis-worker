# Phase 444 — Numeric parsers trim ECMAScript whitespace

## What was wrong

The leading-space scan shared by `parseInt` and `parseFloat` omitted three
ECMAScript WhiteSpace code points: U+1680 OGHAM SPACE MARK, U+205F MEDIUM
MATHEMATICAL SPACE and U+3000 IDEOGRAPHIC SPACE. Thus `parseInt('\u168042')`
returned NaN rather than 42. The same functions are exposed on `Number`.

## What changed

The scan recognizes each complete UTF-8 sequence, alongside the previously
supported whitespace. It still stops on non-whitespace and on incomplete or
unrelated sequences; parsing and conversion after the prefix are unchanged.

## Evidence

`python3 tools/conformance/verify_parse_numeric_whitespace.py` compares 444
Node cases: each ECMAScript whitespace character, non-whitespace neighbors,
combinations and controls for signed zero, Infinity and hexadecimal prefixes,
through the global functions and their Number aliases. **400/444 before,
444/444 after**. The separate engine suite exercises the missing characters,
non-whitespace controls, multiple spaces, signed zero and prefix stopping.
Pinned test262 for the four parser directories improved **104/111 → 106/111**,
two gained and none lost:

```sh
python3 tools/conformance/run_test262.py --binary before=build/litebird-whitespace-before --binary after=build/litebird built-ins/parseInt built-ins/parseFloat built-ins/Number/parseInt built-ins/Number/parseFloat
```

## Still open

This targeted scan is not an audit of all numeric string conversion or radix
rounding. It relies on the engine's existing UTF-8 representation of strings.
