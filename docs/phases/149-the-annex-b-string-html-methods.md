# Phase 149 — the thirteen `String.prototype` methods Annex B kept

Task row: **the Annex B `String.prototype` HTML methods** — a row of its own,
because nothing else in the engine was missing all thirteen of one family, and
the whole of `annexB/built-ins/String` was 19 of 111. `src/js/BuiltinsString.cpp`
was named by no open pull request. Finding 154.

## 1. What was wrong

ECMA-262 Annex B keeps thirteen methods that wrap the string in an HTML
element — `anchor`, `big`, `blink`, `bold`, `fixed`, `fontcolor`, `fontsize`,
`italics`, `link`, `small`, `strike`, `sub`, `sup` — because pages written
before the DOM had an API used them, and every browser still has them. None
existed here:

```js
typeof 'x'.bold          // "undefined"
'x'.anchor('a')          // TypeError: 'x'.anchor is not a function
String.prototype.trimLeft === String.prototype.trimStart   // false
```

Two things were wrong in that last line:

- **Thirteen methods absent.** `annexB/built-ins/String` was **19 / 111
  (17.1%)**, and 82 of the 92 failures were these thirteen — 6 or 7 tests each:
  the output, the attribute's coercion and escaping, `length`, `name`, the
  property descriptor, `new` refusing, and `this`/the attribute converting.
- **`trimLeft` and `trimRight` were second functions doing the same work.**
  §B.2.3.13 says the function object that is the initial value of
  `String.prototype.trimLeft` *is* the one that is the initial value of
  `trimStart`, so they compare `===` and both are named `trimStart`. Here they
  were two natives with two bodies, so `String.prototype.trimLeft.name` was
  `"trimLeft"` and the identity was false — 2 of `trimLeft`'s 4 tests and 2 of
  `trimRight`'s.

## 2. What changed

Everything is in `src/js/BuiltinsString.cpp`.

- **`CreateHTML` (§B.2.3.1)**, once, as a lambda the thirteen share: `<tag>`,
  then ` attribute="value"` when there is an attribute, then `>`, the string,
  `</tag>`. The order is the standard's — the receiver is converted first, then
  the attribute — which is what makes an attribute whose `toString` throws
  throw *after* the receiver's has succeeded, and what the suite checks.
- **The only escaping is in the attribute**: each `"` becomes `&quot;`, because
  that is the one character that would end the attribute it sits in. `<` is
  not escaped, so `'<'.anchor('<')` is `<a name="<"><</a>`, as §B.2.3.2 has it.
- **UTF-16, not UTF-8.** The result is built as `std::u16string` unit by unit
  from `JsString::unit_at`, so a lone surrogate or an emoji in the receiver or
  in the attribute comes out as the same code units it went in as.
- **`trimLeft`/`trimRight` are installed as the very same `JsValue`** as
  `trimStart`/`trimEnd` — `define_str_method` now returns the function it
  made, and `define_str_value` installs a value under a second name.
- The thirteen are installed with the attributes every built-in method has
  (`writable: true`, `enumerable: false`, `configurable: true`), `length` 1 for
  the four that take an attribute and 0 for the nine that do not, the method's
  name as `name`, and `not_constructor` set so `new 'x'.bold` is the
  `TypeError` it should be.

## 3. How it was verified

Two builds, the same 1,086 tests, `annexB` before and after:

```bash
python3 tools/conformance/run_test262.py \
    --binary before=/tmp/before/build/litebird --binary now=build/litebird annexB
#   before     585 pass (53.9%)  [fail 501]
#   now        671 pass (61.8%)  [fail 415]
#   86 pass in now and not in before; 0 pass in before and not in now
```

and the directory this phase is about:

```bash
python3 tools/conformance/run_test262.py annexB/built-ins/String
#   litebird   105 pass (94.6%)  [fail 6]       (was 19 pass, 17.1%)
```

The six that still fail are `match`, `matchAll`, `replace`, `replaceAll`,
`search` and `split` with an **`[[IsHTMLDDA]]`** object, which is `$262`'s —
this engine has no `$262`, so they fail at `ReferenceError: $262 is not
defined`, not at the method. Nothing in this phase can reach them.

The three directories that must not move did not:

```bash
# built-ins/String      before 1200/1223   now 1200/1223   (0 gained, 0 lost)
# language              before 19783/23015 now 19783/23015 (0 gained, 0 lost)
```

and the engine's own suite, with the new `annex_b_string_html_methods` (16
checks: the thirteen outputs, `&quot;`, the missing-attribute `undefined`, a
numeric `this`, a boxed `String` `this`, a nullish `this`, a throwing `this`,
a throwing attribute, an emoji on both sides, `new` refusing, `length`/`name`
for all thirteen, the descriptor, and both trim identities):

```bash
make test     # All 304 engine test suites passed successfully!  (114 cross-origin, 0 failure(s))
```

## 4. What is still open

- **The six `[[IsHTMLDDA]]` tests** need `$262.IsHTMLDDA`, which means a
  document.all-like object — a host object, not a method.
- **`fontcolor`/`fontsize` accept anything**, as the standard says: the value is
  `ToString`ed and not validated, so `'x'.fontsize('big')` is
  `<font size="big">x</font>`.
- The methods are Annex B's and could be removed by a host that does not want
  them; this engine keeps them, as browsers do.
- `annexB`'s other families are untouched here: `Date`'s legacy getters and
  setters (7 of 24), `RegExp`'s `compile` and statics (36 of 62), `escape`/
  `unescape` (29 of 35), and the `Symbol.match`/`replace` hooks with an
  `[[IsHTMLDDA]]` argument.
