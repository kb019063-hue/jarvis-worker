# 23. ✅ What the differential run found

> Moved out of `AUDIT.md` — written there as a `## 23. ✅ What the differential run found` section.
> The text below is the section as it stood, unchanged.


The same run (finding 22) left 1.2% of expressions disagreeing with Node. The
causes that real pages meet were fixed, each with corpus cases:

- **`undefined` was a keyword.** `function (window, undefined) { ... }`, the
  opening line of jQuery-era libraries, was a syntax error. It is now an
  ordinary identifier bound to a read-only global, as are `NaN` and `Infinity`:
  assigning to them does nothing in sloppy code and throws in strict code.
- **Every function printed as `function f() { [native code] }`.** Libraries
  test for built-ins with `/native code/.test(fn)`, so every user function
  passed as a built-in. Functions and classes now keep their extent in the
  source and print it.
- **Lookups stopped 10,000 objects up a prototype chain** and answered "not
  found". The limit existed because nothing stopped a script from making a
  chain circular. `Object.setPrototypeOf`, `Reflect.setPrototypeOf` and
  `__proto__` now refuse to close a loop, and the limit is gone.
- `new String('abc')[1]` was `undefined`, and `Object.keys` of a String object
  was empty.
- `-2 ** 2` was accepted; it is a syntax error.
- `escape` and `unescape` were missing.
- `Math.hypot(1e300, 3)` overflowed to `Infinity`; `parseInt` of a long digit
  string was off in the last digits.
- `Date.prototype.toString` named the zone `(UTC)` where browsers print
  `(Coordinated Universal Time)`.

Left as they are, and listed in ARCHITECTURE.md: arrays are dense, last-digit
differences in `Math.atan2`, `cbrt` and `pow`, and date strings such as
`new Date('1')`.
