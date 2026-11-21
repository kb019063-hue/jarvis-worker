# Finding 154 — thirteen `String.prototype` methods were not there

**Resolved by Phase 149** (`docs/phases/149-the-annex-b-string-html-methods.md`).

## 1. What was found

`annexB/built-ins/String` was **19 of 111**. The suite's own directory listing
said why: `anchor`, `big`, `blink`, `bold`, `fixed`, `fontcolor`, `fontsize`,
`italics`, `link`, `small`, `strike`, `sub`, `sup` — thirteen directories, 82
tests, every one of them failing, and none of the thirteen a method:

```js
'x'.anchor('a')                                          // TypeError
String.prototype.trimLeft === String.prototype.trimStart  // false
String.prototype.trimLeft.name                           // "trimLeft"
```

The trim pair is the interesting half. Both worked, so nothing in the engine
looked wrong; but §B.2.3.13 does not say that `trimLeft` behaves like
`trimStart`, it says the function object **is** `trimStart`'s, so the identity
and the `name` are observable and were both wrong.

## 2. What was done

`CreateHTML` (§B.2.3.1) as one shared lambda behind all thirteen, building the
answer in UTF-16 so a lone surrogate survives, escaping only `"` in the
attribute as `&quot;`; `define_str_method` returning the function it made so
`trimLeft`/`trimRight` could be installed as the same objects as
`trimStart`/`trimEnd`.

## 3. Evidence

`annexB/built-ins/String`: **19/111 → 105/111 (94.6%)**. Whole `annexB`:
**585 → 671**, 86 gained and **0 lost**. `built-ins/String` (1200/1223) and
`language` (19783/23015) identical before and after. `make test` 309/309, new
suite `annex_b_string_html_methods`.

## 4. What is still open

- The six remaining failures in that directory are `$262.IsHTMLDDA` tests: they
  need a host object, not a method.
- `annexB`'s other families: `Date`'s legacy accessors, `RegExp.compile`, and
  `escape`/`unescape`'s remaining cases — none of them this phase's.
