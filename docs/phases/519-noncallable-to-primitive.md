# Phase 519: a present primitive-conversion hook must be callable

## What was wrong

The remaining GetMethod gap recorded in Phase 518 was in
`src/js/JsOps.cpp`: `js_to_primitive` read Symbol.toPrimitive and called
it if callable, but otherwise silently used OrdinaryToPrimitive.
GetMethod permits that fallback only for undefined or null. Any other
noncallable value must produce TypeError before looking up either
`valueOf` or `toString`.

This affects every caller of the shared conversion helper, rather than
just Dates. For example, `Number({[Symbol.toPrimitive]: 0, valueOf() {
return 7; }})` incorrectly returned 7 instead of throwing.

## What changed

After the existing single property read, the helper rejects a non-nullish,
noncallable exotic value with TypeError. Existing callable dispatch,
primitive result handling, hint selection and ordinary fallback are left
unchanged. There is no new general GetMethod abstraction and no change
to other conversion helpers or to built-in-specific fast paths.

The regression suite tests numeric, string and default hints through eleven
callers: Number, String, addition, subtraction, relational comparison,
loose equality, the Date constructor, a Date setter, Math.abs, isFinite and
isNaN.
For each caller it tests eleven noncallable values, undefined/null opt-out,
callable receiver/hint/argument handling, getter and call exceptions,
object results, inherited hooks, mutation of the hook during its getter,
and collection during the getter. It also covers Date and Symbol wrappers
and right-operand short circuiting after a left conversion failure.

## Verification

The standalone suite is `tests/suites/519-noncallable-to-primitive.cpp`.
It asserts an explicit final **`ok 223`** result in C++, not merely a
successful script evaluation. Node v22.22.3 with `--expose-gc` produces
`ok 223` for the same script.

Measured on 28 November 2026 against base `22aa96b` (the claim commit
changed no files). The optimized baseline binary was saved before editing
JsOps; it fails **157 of the 223 checks**, whereas the fixed engine returns
**`ok 223`**. Extract and run the exact suite script without maintaining a
second copy:

```sh
python3 - <<'PY'
from pathlib import Path
import subprocess
source = Path('tests/suites/519-noncallable-to-primitive.cpp').read_text()
js = source.split('R"JS(', 1)[1].split(')JS"', 1)[0]
probe = 'console.log(' + js.strip().removesuffix(';') + ');'
for command in [['node', '--expose-gc', '-e', probe], ['build/litebird', '--run-js', probe]]:
    subprocess.run(command, check=True)
PY
```

At pinned test262 revision `2e58f87d8350ad51224617a041c6e041d52f8444`:

```sh
python3 tools/conformance/run_test262.py \
  built-ins/Number built-ins/String built-ins/Date built-ins/Math \
  built-ins/isFinite built-ins/isNaN language/expressions/addition \
  language/expressions/subtraction language/expressions/equals \
  language/expressions/less-than language/expressions/unary-plus \
  --binary before=build/litebird-before-519 --binary now=build/litebird \
  --json build/519-conversion-test262.json
python3 tools/conformance/run_test262.py \
  --binary before=build/litebird-before-519 --binary now=build/litebird \
  --json build/519-default-test262.json
```

The first comparison improves **2,596 → 2,600 / 2,725**, four gained and
zero lost. It fixes isFinite/isNaN's `toprimitive-not-callable-throws.js`
and String.prototype.indexOf's `position-tointeger-toprimitive.js` and
`searchstring-tostring-toprimitive.js`.

The runner's 21 default helper-regression directories improve
**15,236 → 15,240 / 15,686**, four gained and zero lost, with 14 module
cases skipped. Its four fixes are class declaration/expression computed
name evaluation-error cases `computed-name-toprimitive-returns-noncallable.js`
and `computed-name-toprimitive-returns-nonobject.js`. The two compared
sets do not overlap: **eight distinct newly passing tests**. Neither run
is all test262, and these scores are not an engine-wide conformance claim.

An explicit **-O0 -g0 ASan/UBSan** full-engine build returns **`ok 223`**,
including collection during invalid-hook getters:

```sh
make -j2 BUILD_DIR=build-asan-fast \
  CXXFLAGS='-std=c++20 -O0 -g0 -Wall -Wextra -Iinclude -MMD -MP -fsanitize=address,undefined -fno-omit-frame-pointer' all
# Run the extracted probe with ASAN_OPTIONS=detect_leaks=0 and
# build-asan-fast/litebird in place of build/litebird.
```

All **eight newly passing test262 cases** also pass under ASan/UBSan.
The broader 2,725-case sanitizer run scores **2,599 pass, 125 fail,
one timeout**, with no sanitizer reports. The timeout is
`built-ins/String/prototype/repeat/empty-string-returns-empty.js`, which
repeats an empty string 2,147,483,647 times. It times out at both 20 and
120 seconds in this explicit -O0 build. Both runs retain `--min-pass 2600`
and exit nonzero; this is recorded as a validation limit, not counted as
a passing gate or hidden by lowering its floor. The optimized plain build
passes this case both before and after the guard. This task does not
change String.prototype.repeat's implementation.

Broader plain-build validation:

| Command | Result |
| --- | --- |
| `make test` | **560/560** engine suites, **114/114** cross-origin checks |
| `make conformance` | **1,973/1,973** |
| `make js-conformance` | **1,153/1,158**, everyday **90/90**, no crashes/timeouts |
| `make dom-conformance` | **678/678** |
| `make url-conformance` | **1,188/1,188** |
| `make xml-conformance` | **1,718/1,718** applicable cases |
| `make wpt` | **9,570/9,824**, only the 254 listed failures |
| `make diff-trees` | **6,000/6,000** match corrected parse5 |
| `make diff-xml` | 4,000 documents, zero disagreement where both references agree; **3,000/3,000** round trips |
| `make render-conformance` | **67/68** original pages (recorded AVIF decoder gap), **41/41** body-origin pages, **45,016/45,016** canvas checks |

`diff-xml` used `lxml` installed only in ignored `build/519-python-deps`
and added to `PYTHONPATH`. No metrics ledger, corpus expectation or floor
was edited. A later claim (#522) also names JsOps.cpp for generator/GC
work; its agent was notified of this task's four-line guard and the tasks'
different scopes.

Validation limits: default `make fuzz` initially stops because it requires
`build-asan/litebird`, whereas the explicit sanitizer build is in
`build-asan-fast`. The parser, pipeline and XML mutation scripts are run
with that binary explicitly instead; this is not the full fuzz aggregate.
Those explicit ASan/UBSan runs pass: parser **400 mutations**, pipeline
**400 runs** (150 HTML inputs through layout/render and 100 image cases),
XML **600 mutations** plus **1,927 unmodified documents** and their
round trips, all with **zero findings**.
`make deep` was started with its defaults (depth 200,000, 300 random
pages) but stopped after about 25 minutes on this two-core sandbox. Three
runs (table dumps and details dumps/terminal) reached their 60-second
parallel time limits and were queued by the harness for standalone retry;
the run had not reached its final retry/summary stage. This is an
**incomplete check, not a passing deep gate**. It overlapped the sanitizer
comparison and explicit fuzz checks for part of that time.

The default -O1 full `make asan` aggregate is not run locally. The landing
session must pass full CI, including the complete deep-page jobs and its
default sanitizer configuration, before merging. Focused -O0 checks do
not replace those gates.

## What is still open

ToPropertyKey's preservation of a Symbol returned from object conversion,
Proxy/class-callability behavior and built-in-specific conversion fast
paths are separate issues. This task implements GetMethod's noncallable
check only at the shared ToPrimitive dispatch site, not a repository-wide
GetMethod refactor or full ECMAScript conformance claim.
