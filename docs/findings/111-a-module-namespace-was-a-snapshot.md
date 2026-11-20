# Finding 111 — a module namespace was a snapshot, so `export *` could not exist

Phase 105. Status: **resolved** for `export *`; the liveness half is open and is
described in §3.

## 1. What was wrong

`export * from './mod.js'` was refused by the module transform with

```
export * is not supported without live module bindings
```

which was true and was also the reason the feature could not simply be added.
`evaluate_source` built a module's namespace as a **copy** of the wrapper's
export bag:

```
JsValue exports = JsValue::make_object(...);
... wrapper fills it ... (exports_name)[name] = local_name;
record->exports = exports;              // a snapshot, handed to every importer
```

A snapshot knows only the names the wrapper wrote. `export *` does not know its
names until the source module has been evaluated, so there was nothing to copy
at the moment the copy was made.

## 2. What the fix was

Make the namespace a set of accessors that resolve when read, and let a star
entry republish another module's namespace beneath the module's own names:

- `ModuleTransform::star_exports` records each `export * from './m.js'`;
- the dependency is added with no `ModuleImportBinding`s, because no name is
  known yet;
- `make_live_namespace(own_bag, stars, referrer_url)` defines one enumerable
  getter per own key, then one per key of each starred namespace, skipping
  `default` and skipping any name already defined.

Reading `s.base` runs a native getter that reads the source bag at that moment,
so a chain of stars resolves at any depth through the same mechanism.

## 3. What is still open — and why it is not `export *`'s fault

The accessors read the right bag, but **two evaluations of one module are two
instances**:

```
counter.js   export let n = 1; export function bump() { n = n + 1; }
entry.js     import { n, bump } from './counter.js';
             var before = n;  bump();  var after = n;      // 1, 1
```

`evaluate_dependency` resolves the specifier to a URL and calls
`evaluate_source`, whose `m_records` key for the dependency is not the key the
entry module was recorded under, so the module is evaluated again. `bump`
closes over the second instance's `n`; the namespace reads the first. The same
program with no `export *` in it answers the same way, which is what makes this
finding's scope narrower than "module bindings do not work".

This is asserted rather than assumed: `tests/test_suite.cpp`,
`module_worker_static_graph_loading_and_refusal`, pins `liveReply.after == 1`
with a comment saying what the standard answers and why the engine does not.

Closing it needs one module instance per record and an indirection from an
imported name to that instance's binding slots — the module-record machinery
Phase 66 stated it did not have. It is left as the row it already is, not as
work done here.

## 4. What else this does not cover

`export * as ns from` (own message), dynamic `import()`, `import.meta`,
top-level `await`, cycles (still refused, by name), module `<script>` elements,
and transferable messaging — the last blocked on `ArrayBuffer`, which script
cannot see at all.
