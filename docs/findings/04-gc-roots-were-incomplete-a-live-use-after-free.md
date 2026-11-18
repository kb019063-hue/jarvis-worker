# 4. ✅ GC roots were incomplete — a live use-after-free

> Moved out of `AUDIT.md` — written there as a `## 4. ✅ GC roots were incomplete — a live use-after-free` section.
> The text below is the section as it stood, unchanged.


> **Fixed in `f78d201`.** The heap now has a root-provider registry; the
> interpreter and the event loop both contribute their live `JsValue`s. Verified
> with 200 consecutive empty-root-set collections under ASan. The original
> finding follows.
>
> **Superseded in Phase 23.** Registering roots at every boundary turned out
> not to be completable; finding 19 is what was still missed. The collector
> now derives its roots from reference counts and nothing is registered.


`JsObject` lives in a mark-and-sweep heap holding **raw `JsObject*`**. A
`JsValue` is a `shared_ptr`-managed value that can point at one. Nothing keeps
a `JsObject` alive except being reachable from the root set at collection time.

The root set is only:

```cpp
for (auto& v : m_stack)   roots.push_back(&v);   // VM operand stack
for (auto& [k, v] : m_globals) roots.push_back(&v);
```

`register_root()` exists in the API and **is never called anywhere**. So
`JsObject*` reachable only through:

- a closure environment (`JsEnvironment` holds `JsValue`s in `shared_ptr`s),
- a `NativeFn` lambda capture,
- a registered event listener,
- `Element::m_custom_element_instance`,

…is invisible to the collector and can be freed while live. `Heap::sweep()`
deletes the cell outright; every surviving `JsObject*` then dangles.

**It does fire, and it is not latent.** I originally wrote here that nothing
triggered a collection — that was wrong. `BrowserShell::render_terminal_frame`
collects on **every DevTools render** with an empty root list:

```cpp
if (devtools) {
    auto heap_stats = Heap::default_heap().collect_garbage();   // no roots
```

Against `examples/interactive.html` that prints `alive 23 -> 0` and ASan reports
the next property read as a `heap-use-after-free` in `Heap::sweep()`. Values
captured by pending promise handlers are hit the same way, and silently return
`undefined` instead of their data.

**Fix direction:** either make `JsValue` hold `shared_ptr<JsObject>` and drop
the custom collector, or register roots properly at every boundary
(`JsEnvironment`, native captures, listeners) and clear `is_marked` in one pass.
Also note `Heap::default_heap()` is a process-wide singleton with a mutex —
worker threads allocate from the *same* heap as the main thread, so a
collection on one thread can free objects the other is using.

---
