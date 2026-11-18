# 19. ✅ `gc()` freed objects that were still in use

> Moved out of `AUDIT.md` — written there as a `## 19. ✅ `gc()` freed objects that were still in use` section.
> The text below is the section as it stood, unchanged.


Finding 4 was fixed by registering roots. After Phase 23 every object is a
heap cell, which made every unregistered holder a use-after-free the next time
script called `gc()`:

```js
function inner() { gc(); }
function outer() { var o = {a: [1, 2, 3]}; inner(); return o.a.length; }   // o freed inside inner()

for (let o = {a: 1}, i = 0; i < 2; i++) { gc(); o.a; }                     // the loop's scope was not a root

button.addEventListener("click", function () { state.count++; });        // `state` lives in a std::function
gc(); button.click();
```

AddressSanitizer reported each as `heap-use-after-free`. The same shape exists
wherever native code keeps a value: observers, timers, promise reactions, a
half-evaluated argument list, the stack of a suspended generator.

**Fix.** The collector no longer depends on registration. Each `JsValue` holds
a counted handle to its cell; a collection counts the references it can find
inside the heap and treats anything with more references than that as held by
native code, hence a root (`Heap::collect_garbage`, described in
`ARCHITECTURE.md`, Phase 23).

**Tests.** `tests/js-conformance/gc-safety.dat` has 32 cases that collect in
the middle of something (the first two above among them) and must still print
Node's answer. The listener case needs a DOM, so it is a unit test,
`gc_keeps_state_reachable_only_from_listeners_and_frames`. `make asan` runs
both under the sanitizers, and the corpus runner now fails the run on any
sanitizer report in a case's stderr, whatever the pass count: UBSan prints its
report and carries on, so a case could otherwise print the right answer after
undefined behaviour and count as a pass.
