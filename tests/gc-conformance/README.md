# Collection stress corpus

28 cases, each a small program whose `#expect` was recorded from **Node.js**,
not from Litebird (`tools/conformance/add_js_case.py`'s rule).

They are not here because they compute something interesting. They are here
because each one holds a value alive across an allocation in a place where the
collector has to find it without being told:

| File | What is held where |
| :--- | :--- |
| `closures.dat` | a scope alive after the call that made it, a `let` per loop iteration, a block's bindings after the block, a function's own property cell, a named function expression naming itself |
| `iterators-generators.dat` | a suspended generator's locals across a `yield`, a `for-of` iterator, an abandoned generator next to a live one, `yield*`'s delegate, a custom `Symbol.iterator` |
| `maps-and-collections.dat` | `Map` entries, two live cursors across insertion and `clear()`, objects used as keys, `Set` identity, array holes |
| `scopes-and-exceptions.dat` | a caught error kept after the `catch`, a `finally` block's bindings, a per-iteration `catch` scope, an `arguments` object outliving its call, a direct `eval`'s function, a class's private field |
| `builtins-that-call-back.dat` | `sort`, `forEach`, `map` and `filter` re-entering script while the native frame holds the array; a getter that allocates; a JSON round trip |

The last file is the one that matters most: a native frame that keeps a
`JsObject*` into `array_elements` while script allocates is precisely what a
collection at the wrong moment would expose.

## Running it

```bash
make verify-gc-stress          # the negative control, this corpus, and the DOM
make verify-gc-stress-full     # interval 1 over this corpus, the JS corpus, the DOM
```

or, directly:

```bash
python3 tools/conformance/verify_gc_stress.py --seeds 1,2,3 --interval 1 --show 10
```

## What a failure means

A case failing here does **not** mean its output is wrong in the ordinary
sense. It means the program printed two different things on two runs of the
same build, differing only in when the collector ran — so something the
program still needed was freed, or something it had dropped was kept in a way
that changed what it saw. Run the case with and without
`--gc-stress=SEED` and compare; the seed and the collection count are printed
on stderr.

A case whose un-stressed run already disagrees with its `#expect` is reported
as **out of scope**, never as agreement: a program the engine does not run
correctly says nothing about its collector.

## The negative control

`self-test/self-test.dat` is not part of the corpus. `--self-test` runs it and
requires the harness to catch all three cases for the right reason — a count
only a collection can change, a case that already fails, and an interval the
program never reaches. `make verify-gc-stress` runs it first. A harness that
cannot fail is not evidence, and `README.md` carries the receipt for a
scoreboard that reached 100% while being wrong.
