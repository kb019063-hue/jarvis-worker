# Phase 23 Architecture: The JavaScript Engine, Rebuilt

> Moved out of `ARCHITECTURE.md` — written there as a `## Phase 23 Architecture: The JavaScript Engine, Rebuilt` section.
> The text below is the section as it stood, unchanged.


### 1. Why this phase existed

Phase 15 gave the HTML parser an outside scoreboard, and the parser has been
trustworthy since. The JavaScript engine had none: it was measured by unit
tests written next to the code, and they passed. Phase 23 started by building
the missing scoreboard (`tests/js-conformance/`, expected output recorded from
Node.js) and running the engine against it:

| | Everyday probe (90 cases) | Whole corpus (801 cases) |
| :--- | :--- | :--- |
| Engine before Phase 23 | 25 | 140 |
| Engine after Phase 23 | 90 | 796 |

The failures were not edge cases. Reading the engine found the causes:

- The lexer turned any character it did not know (`?`, `&`, `|`, `^`, `~`) into
  end-of-file. The rest of the script was dropped without an error.
- `try`, `catch`, `throw`, `continue`, `do` and `in` were not keywords.
- `eval()` ran the bytecode VM first and, on any exception, ran the whole
  script again in the tree-walker. Every side effect up to the exception
  happened twice.
- Strings had no `length` and no methods. Arrays carried six closures each and
  had no prototype. `Map` keyed by the string form of the key. `Symbol()`
  returned a string. An undeclared name read as `undefined` instead of
  throwing.

Patching these one at a time would have left the two overlapping execution
paths in place, so the front end, the evaluator and the standard library were
rewritten. The DOM bindings, the event model and the host APIs were kept.

### 2. Front end and evaluator

`JsLexer` produces tokens and reports anything it cannot tokenise as a
`JsSyntaxError`. `JsParser` is a recursive-descent parser that builds an AST
(`NodeKind`, `Pattern`, `FunctionNode`, `ClassNode`, `Program`) and resolves
what can be resolved once: which names a function declares with `var`, whether
it uses `arguments`, `super` or `await`, whether a block declares anything and
so needs a scope of its own. A script that does not parse does not run.

`JsEval.cpp` walks the AST. Statements return a completion
(`Flow {Normal, Break, Continue, Return}`) instead of throwing C++ exceptions
for control flow; only a JavaScript `throw` unwinds the native stack, as a
`JsThrow` carrying the thrown value. `eval()`/`eval_ast()` report an uncaught
error on stderr as `Uncaught ...` and count it; `run()` lets it propagate.
Host code that calls into script from a place with nothing above it to catch
(an event listener, a timer) goes through `js_run_guarded`.

Native functions keep the signature `JsValue(const std::vector<JsValue>&)`.
The receiver travels in a per-thread call context that `call_with_this()` and
`construct()` set; natives read `JsValue::current_this()`.

Functions and classes record where they start and end in the script, and the
script text is kept for as long as any of them is alive, so
`Function.prototype.toString` returns the source. That is not cosmetic:
libraries tell built-ins from user code by testing for `[native code]`.

The bytecode compiler and VM from earlier phases still build and back
`--dump-bytecode`, but no script runs on them. They cover a small subset of
the language, and keeping two engines that must agree was the source of the
double execution.

### 3. The standard library and realms

Built-in methods live on prototype objects (`Array.prototype.map` is one
function, not one closure per array). The set of prototypes and constructors
is a realm (`JsIntrinsics`). Each interpreter owns one, so a page that patches
`Array.prototype` does not change it for the next page or for a worker. Code
with no interpreter at hand (host bindings, unit tests) uses the realm current
on its thread, or a process-wide default.

The library is split by area: `BuiltinsObject.cpp` (Object, Function,
Boolean, Symbol, Reflect, the Error family), `BuiltinsArray.cpp`,
`BuiltinsString.cpp`, `BuiltinsNumber.cpp` (Number, Math), `BuiltinsCollections.cpp`
(Map, Set, WeakMap, WeakSet), `BuiltinsJson.cpp`, `BuiltinsRegExp.cpp` (a
backtracking matcher written for this engine), `BuiltinsDate.cpp` and
`Promise.cpp`.

### 4. Promises, async functions and the event loop

A promise's state is a `PromiseData` shared between the script-visible
objects and the engine code that settles it. Reactions always run as
microtasks, first in, first out. Resolving with a thenable adopts it from a
microtask, never synchronously. A rejection is the thrown value itself.

`await` has to stop a function in the middle of an expression and give
control back to its caller. A tree-walking evaluator keeps that state on the
native stack, so a function that can suspend runs on a stack of its own: an
async function whose body contains `await`, like a generator, gets a thread.
The threads never run at the same time. Control is passed with a strict
handshake (`GeneratorData::resume` / `yield_to_main`): the frame's thread runs
only while the thread that resumed it is blocked waiting for it. Script is
therefore still single-threaded, and ThreadSanitizer reports nothing on the
async and generator cases. The cost is one OS thread per suspended call; see
"What is still approximate".

The event loop (`EventLoop`) holds tasks, microtasks and timers. There is a
microtask checkpoint whenever the script stack empties (`JsScriptScope`), which
is what makes a promise callback run after the current script and before the
next timer. The loop has its own clock, which can skip ahead to the next timer
instead of sleeping; `Date.now()` and `performance.now()` follow it. The
command-line paths use that: `--run-js` runs a script's timers to completion
without waiting for them, and a page rendered once settles for one frame of
virtual time (`--virtual-time-budget`) before layout.

A page and each worker is an agent with its own event loop. A worker's timers
and promise callbacks run on the worker's thread, not the page's.

### 5. The collector finds its own roots

Every object is a heap cell and nothing but the collector frees one, so the
collector must never free a cell something still refers to. The engine used to
ask everything that held a value to register it as a root. That cannot be made
complete: values sit in C++ locals of the evaluator, in half-built argument
lists, in `std::function` captures of DOM listeners and timers, on the stack
of a suspended generator. Each one that was missed was a use-after-free the
next time `gc()` ran. Two were found by testing, not by reading: a listener's
closure, and the scope of a `for (let ...)` loop.

The collector now needs no registration.

- Every `JsValue` that points at a cell holds a counted handle (`CellRef`), so
  a cell knows how many references to it exist. Scopes and function payloads
  are already reference-counted by `shared_ptr`.
- A collection counts the references it can enumerate: those stored in cells,
  in scopes and in function payloads.
- Anything with more references than that is held from somewhere the collector
  cannot see. It does not need to know where: the thing is a root.
- From the roots it marks as before. Unmarked cells are freed and unmarked
  scopes emptied, which also collects the cycle every closure forms with the
  scope it was created in.

Two rules keep it sound, and they are stated in `Heap.cpp` next to the code. A
reference is only counted if whatever stores it cannot be kept alive behind
the collector's back; storage native code may co-own (a pending promise's
reactions, for example) is left uncounted, which makes its contents roots. And
every stored reference is counted at most once. Counting too little only keeps
garbage alive, so every doubtful case errs that way.

Cells and scopes are tagged with the agent that created them. A collection
examines and frees only what belongs to the agent running it, so a page
collecting does not look into objects a worker thread is changing.

`tests/js-conformance/gc-safety.dat` runs a collection in the middle of
everything (callbacks, getters, half-built literals, loops, suspended
generators and async functions) and requires Node's answer. `make asan` runs
it under AddressSanitizer, where a freed object still in use is an error.

### 6. Deep JavaScript cannot exhaust the native stack

A recursive-descent parser and a tree-walking evaluator recurse as deep as
their input nests, and C++ destructors recurse as deep as the structure they
free. Both are fine for ordinary code and both end the process on input built
to be deep: 200,000 nested parentheses, or a linked list of a million closures
that is dropped. Stress pages found 28 such cases (AUDIT.md, finding 21).

This section is about the JavaScript engine. The DOM has the same weakness and
it is not fixed: see "Known gaps", item 11.

Two mechanisms close this, and both are deliberately independent of how much
stack one level costs, which differs by a factor of 50 between an optimised
build and a sanitizer build:

- **Recursion that follows the input checks the stack.** `js_check_stack()`
  compares the stack pointer with the end of the thread's stack
  (`common/StackLimit.hpp`) and throws a `RangeError` while a megabyte is
  still left. The parser's recursive entry points call it, as do the
  evaluator's statement and pattern walks, function calls, and the library
  functions that recurse over data (`join`, `flat`, `structuredClone`,
  `JSON.stringify`, the JSON parser). A script that is too deep is reported as
  `Uncaught RangeError: Maximum call stack size exceeded`; the next script on
  the page runs.
- **Recursion that would only free memory is turned into a loop.**
  `common/ReleaseQueue.hpp` is a per-thread queue: a destructor hands over what
  it owns instead of destroying it, and the outermost destructor drains the
  queue. Syntax trees (`ast_delete`), scopes, promise chains and function
  payloads are released this way. Running out of stack while freeing cannot be
  reported to anyone, so it must not be able to happen.

Long operator chains are not nesting, even though `a+b+c+...` parses into a
left-leaning tree as deep as the chain is long. `eval_binary_chain` walks such
a chain in a loop, so a sum of a million terms is evaluated, not refused.

Generators and async functions recurse in a third way. A frame that resumes
another frame waits for it, so `function* g() { yield* g(); }` stacks up
threads instead of stack frames, and no stack check can see that. Frames may
therefore be resumed from inside one another at most 2,000 deep
(`kMaxFrameNesting`); beyond that the script gets the same `RangeError`.
Without the limit the recursion ended when the system refused another thread,
at a depth that differs by machine and can be more than its memory holds.

Prototype chains are walked without a depth limit. They cannot be circular:
`js_set_prototype` is the one place a script can change the prototype of an
object that already exists, and it refuses a change that would close a loop.

### 7. Page lifecycle

`document` is an event target, `document.readyState` exists, and
`JsInterpreter::notify_document_loaded()` fires `DOMContentLoaded` and `load`
after the document's scripts have run. The command-line renderer and the
shell both call it. `removeEventListener`, the `{capture, once}` options and
`this` inside a listener were added on the way.

### 8. How this is verified

| Check | Result |
| :--- | :--- |
| JavaScript corpus (`make js-conformance`) | 796 / 801; every expectation re-checked under Node 22 in CI |
| Differential run against Node: 67,545 generated expressions, results compared | 99 differ (0.15%); it was 835 before the fixes in AUDIT.md findings 22 and 23 |
| Unit tests (`make test`) | 85 / 85 |
| ASan + UBSan: unit tests, the corpus, both fuzzers (`make asan`) | 85 / 85; corpus 795 / 801, the extra failure being a recursion 2,000 deep, which a sanitizer build has no stack for; 400 + 400 fuzz runs; no sanitizer report anywhere |
| Stress pages: 88 pages of deep JavaScript nesting and long chains, normal and ASan builds | no crash and no sanitizer report; the pages that run out of time or memory instead are listed in AUDIT.md, finding 21 |
| ThreadSanitizer: async, generator, iterator and control-flow cases | no reports when run; not repeated since the collector was rewritten |
| html5lib tree construction | 1770 / 1792, unchanged |
| The 16 example pages, DOM and layout dumps against the previous engine | 12 identical; 4 differ because a script that used to stop now runs to the end |

The differential run and the stress pages are generated by scripts that are
not in the repository; what they found is, as corpus cases.

Three existing unit tests were changed. `gc_roots_are_complete_under_empty_root_set`
compares the live cell count with the default realm's permanent cells instead
of zero, because built-in prototypes are now real objects. `js_async_promises_and_json`
calls `then` with its receiver, because `then` now lives on `Promise.prototype`.
`strict_equality_does_not_coerce` constructs a `Document`; it used to pass by
comparing `undefined === undefined`.

### 9. Speed

Milliseconds for the whole process, best of three, one run on one machine;
repeat runs vary by about 30%. An empty script takes Node 21 ms and Litebird
9 ms. "Before" is the engine at the end of Phase 22.

| Script | Before | After | Node 22 |
| :--- | :--- | :--- | :--- |
| 2M-iteration loop | 2098 | 376 | 28 |
| 300k function calls | 1994 | 165 | 28 |
| 200k string concatenations and `length` | 297, wrong result | 56 | 34 |
| 200k `push`, then `map`/`filter`/`reduce` | 5447, wrong result | 254 | 41 |
| 100k object allocations and property reads | 322 | 103 | 32 |
| 200k method calls on a class instance | 1396 | 95 | 27 |
| 2k awaits in sequence | 18 | 243 | 28 |

The rebuilt engine is 3 to 15 times faster than the one it replaced, except
at `await`, where it now pays for a thread (section 4) and the old engine did
not suspend at all.

These scripts are too short to compare with Node, whose figures are mostly
start-up. On work long enough for that not to matter, a 20-million-iteration
loop takes Litebird 7.1 s and Node 0.11 s, and 3 million function calls take
2.8 s and 0.05 s: a tree-walking interpreter is 50 to 100 times slower than a
JIT, and this one is no exception.

**Where it was worse than that.** A string was stored inside the value that
held it, so reading a string variable copied the string, and `length` and
indexing scanned it. A loop over 200,000 characters took 76 s where Node takes
35 ms. That is fixed: see Phase 24 below. Two containers had the same
shape:

| Script | Litebird before Phase 30 | Node |
| :--- | :--- | :--- |
| `delete` of 50,000 keys from one object | over 120 s | 91 ms |
| `shift()` until a 50,000-element array is empty | 5.3 s | 0.2 s |

The property map rebuilt its index on every `delete`, and arrays were plain
vectors. Phase 30 removed both.

### 10. What is still approximate

- **One OS thread per suspended function.** Each generator and each async call
  that is waiting on an `await` holds a thread. Thousands at once work; the
  system's thread limit is the engine's limit, reported as a `RangeError`.
  Starting one costs about a tenth of a millisecond: 2,000 awaits in sequence
  take 240 ms where Node takes 28.
- **Recursion depth.** A JavaScript call costs about 2 KB of native stack, so
  a default 8 MB stack gives roughly 3,000 nested calls before
  `RangeError: Maximum call stack size exceeded`. Node allows about 12,000.
  Generators delegating to generators, and async functions calling async
  functions before their first `await`, may nest 2,000 deep.
- **No automatic collection.** The collector is sound and collects cycles, but
  it only runs when `gc()` is called or the shell's heap view is opened.
- **Missing language features:** BigInt, `Proxy`, typed arrays and
  `ArrayBuffer`, modules, `with`, and holes in arrays (`delete a[1]` leaves
  `undefined`, and `1 in a` stays true).
- **Approximate library corners:** `localeCompare` is not real collation,
  `normalize()` returns its string unchanged, case mapping covers Latin, Greek
  and Cyrillic, Unicode property escapes in regular expressions are
  approximate (`\p{L}` misses fullwidth letters and letters outside the BMP),
  there is no `Intl`, regular expressions have no `d` flag, and
  `WeakMap`/`WeakSet` hold their keys strongly. The first four are all missing
  Unicode data; `tools/conformance/diff_strings.py` lists the results they
  change.
- **`console.log` of an object** prints `[object Object]`, not its contents.
- **`await` outside an async function** (the top level of a script, an async
  generator body) blocks and runs the event loop instead of suspending.
- **Workers** have their own interpreter, event loop and collector scope, but
  allocate from the same heap as the page. In the one-shot command-line modes
  (`--run-js`, rendering a page once) the process exits when the page's own
  event loop is idle, without waiting for a worker's reply; workers are only
  usable in the shell.
- **Early errors.** Duplicate labels and `break` to an unknown label are not
  rejected when the script is parsed.

---
