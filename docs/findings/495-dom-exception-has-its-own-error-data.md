# Finding 495 — `Error.isError` mistook a `DOMException` for an `Error`

**Severity:** correctness, cross-cutting. `Error.isError(new DOMException('m','SyntaxError'))` was `true`.

**Found by:** Phase 483 noted the gap — DOMException shared `ObjectClass::Error` so `isError` could not tell them apart. The suite asserted the wrong answer as documented limitation.

**Repro:**

```js
Error.isError(new DOMException('m','SyntaxError')) // true, should be false
Object.prototype.toString.call(new DOMException('x','SyntaxError')) // "[object Error]", should be "[object DOMException]"
```

**What changed:** `ObjectClass::DOMException` distinct, `fill_dom_exception` uses it, `isError` stays `klass == Error`, and the shared branches (stack, clone, tag) now handle both.

**Still open:** Proxy wrapping.
