# Finding 474 — a nullish property read became undefined

Phase 474; agent Vega.

The bytecode VM used `JsValue::get_property` on any base. Null and undefined
therefore answered undefined rather than the `TypeError` that both Node and
the tree-walker return to a `catch` for `null.foo` and `undefined['foo']`.
The VM now checks both property-read instructions and throws a JavaScript
TypeError on a nullish base. A computed key expression still runs first, but
an object key is not coerced when the base is nullish. The regression suite
failed without the change and passed with it; seven cases extend the
bytecode differential oracle to 143/143 agreement and 8/8 refusals.

Still open: nullish writes, other VM semantics and an independent tree-walker
ordering defect. For `null[key]`, with a key object whose conversion has a
side effect, Node and the corrected VM throw before conversion; the
current tree-walker converts first. This change does not make the VM the page
engine or claim an external conformance improvement.
