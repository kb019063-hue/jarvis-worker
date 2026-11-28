# Phase 546 — the VM rejects non-callable calls with TypeError

Task: the bytecode differential-oracle row's non-callable-call defect;
finding 546; agent Vega.

## What was wrong

`try { (0)(); } catch (e) { e.name; }` returns `TypeError` in Node
v22.22.3 and the tree-walker, but the VM returned `Error`. `OpCall` threw a
C++ `std::runtime_error`, which the VM's exception handler converted to a
generic JavaScript Error. Its error message also called `callee.to_string()`:
for an object this can execute `toString` or throw a different exception,
although IsCallable never coerces the value.

## What changed

After the compiled arguments have run, `OpCall` uses `js_throw_type_error`
for a non-callable value without converting it. The VM's existing handler
unwinds frames, finalizers and iterators as for other JavaScript throws. The
new generic message is `expression is not a function`; the opcode does not
carry the source identifier needed for the tree-walker's named diagnostics.

## Verification

`tests/suites/546-vm-noncallable-calls.cpp` failed on the original witness
before the fix and passes afterwards. Node checked its 17 differential traces:
null, undefined, numbers, strings, booleans, arrays, objects and symbols;
TypeError prototype; argument ordering and throwing arguments; object
conversion side effects and exceptions; frame, finalizer and iterator cleanup;
a valid call; and an uncaught JavaScript throw. The independent bytecode
oracle gains eleven cases: `make verify-bytecode` reports 199/199 agreements
and 8/8 still refused. `make test` passes 574 suites and 114 cross-origin
checks. HTML conformance is 1973/1973, JS 1154/1158 with a 90/90 everyday
probe, DOM 678/678, URL 1188/1188, XML 1718/1718, and WPT 9570/9824 with no
unlisted failures. `diff-trees`, `diff-xml`, `check-numbers`, `check-suites`
and `check_docs.py` pass.

## Still open

The VM has separate non-callable diagnostics for named expressions: it uses
`expression` rather than the tree-walker's source name. `OpGetProperty`
currently binds every function-valued property, so `e.constructor === TypeError`
is false even for the existing nullish-read TypeError; checking the prototype
with `Object.getPrototypeOf(e) === TypeError.prototype` succeeds. This affects
function identity in general and is not fixed here. Nullish dotted writes,
other VM semantics, and non-nullish tree-walker computed assignment/update
conversion order remain separate work. No external conformance or performance
improvement is claimed.
