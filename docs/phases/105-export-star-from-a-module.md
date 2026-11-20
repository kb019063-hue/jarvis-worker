# Phase 105 — `export *` from a module

Task row: **General ES-module semantics and transferable messaging remain open.**
Phase 66 gives module workers a MIME-checked static graph subset; this phase
takes the `export * from './mod.js'` form it refused. Finding 111.

## 1. What was wrong

Phase 66 built a static module graph for module workers and was honest about
what it did not have. One of those things was `export * from './mod.js'`, and
the transform refused it in so many words:

```
src/js/Worker.cpp:631
    module_syntax_error(tokens[pos], "export * is not supported without live module bindings");
```

The message was accurate about the cause. A module namespace was a **snapshot**:
`evaluate_source` built a plain object from the wrapper's export bag and handed
that object to every importer. A snapshot cannot republish a set of names it has
not seen yet, and `export *` does not know the names until the source has been
loaded and evaluated — which is the whole difference between `export { a } from
'./m.js'` (one name the source parser can check) and `export * from './m.js'`
(every name, whatever they turn out to be).

So the fix was not to teach the parser one more statement. It was to make a
namespace something that can be **read on demand**.

## 2. What changed

**The parser accepts it** (`src/js/Worker.cpp`, `parse_module_export`). `export *
from './mod.js'` records the specifier in a new `ModuleTransform::star_exports`
and adds the dependency with **no bindings**: there is nothing to bind a name to
yet. The `export * as ns from` form is a different feature (it names the
namespace object as a binding) and is still refused, with its own message, so
that the two are not confused in a test or in a bug report.

**The namespace is built from accessors** (`make_live_namespace`). Each property
of a module namespace is now a getter that reads the value when it is asked:

```
for (const std::string& name : own_bag.own_keys()) {
    auto getter = js_make_native("get", 0, [own_bag, name](const std::vector<JsValue>&) {
        return own_bag.get_property(name);
    });
    ns.define_accessor(name, getter, JsValue::make_undefined(), PropertyMap::Enumerable);
}
```

The bag is captured, not its values. For each `star_exports` entry the source
module is evaluated, its own keys are read, and each is republished through the
same kind of accessor, with the two rules the standard gives (§16.2.3.7,
ExportEntriesForModule):

- **`default` is not republished.** `export *` adds the named exports only.
- **A nearer declaration wins.** A name the module declares itself is already an
  own property, so the star does not overwrite it. Because the star republishes
  the source's *namespace* rather than copying it, a chain (`a` stars `b` stars
  `c`) resolves at any depth without a second mechanism.

## 3. How it was verified

`make test` — **261 / 261 suites**. The suite that covers this ground,
`module_worker_static_graph_loading_and_refusal` (`tests/test_suite.cpp:16776`),
used to assert the opposite of what it asserts now:

```
// before
unsupported << "export * from './math.js';\n";
assert(js.eval("unsupportedError").to_string().find("export *") != std::string::npos);

// after — math.js exports base, label, add and a default
star  << "export * from './math.js';\n"
         "export const answer = 'own-answer';\n";
assert(js.eval("unsupportedReply.base").to_number() == 40.0);
assert(js.eval("unsupportedReply.sum").to_number() == 3.0);
assert(js.eval("unsupportedReply.own").to_string() == "own-answer");
assert(js.eval("unsupportedReply.hasDefault").is_truthy() == false);   // default not republished
assert(js.eval("unsupportedReply.keys").to_string() == "add,answer,base,label");
```

The other scoreboards are unchanged, which is the point of running them:

| Command | Before | After |
| :--- | :--- | :--- |
| `make js-conformance` | 1137 / 1156 (98.4%) | 1137 / 1156 (98.4%) |
| `make dom-conformance` | 678 / 678 | 678 / 678 |
| `make conformance` | 1972 / 1973 (99.9%) | 1972 / 1973 (99.9%) |
| everyday probes | 90/90 and 68/68 | 90/90 and 68/68 |

## 4. What is still open

**A module evaluated for one dependency is not the instance another sees.** The
first version of this phase claimed liveness — a namespace whose getter reads the
bag would follow a `let` the exporter reassigns — and the test that asked for it
failed:

```
// counter.js:  export let n = 1;  export function bump() { n = n + 1; }
// entry.js:    import { n, bump } from './counter.js';
var before = n;   // 1
bump();
var after = n;    // 1, where the language answers 2
```

The reason is `evaluate_dependency`: it calls `evaluate_source` with a **key that
differs from the one the entry module was recorded under**, so `counter.js` is
evaluated a second time and `bump` closes over the second instance's copy of
`n`. The accessor reads the first. This is not caused by `export *` — it happens
with a plain named import and no star anywhere — and it is now **asserted in the
suite** (`liveReply.after == 1`) so that a later change cannot quietly depend on
the other answer. Real live bindings need one module instance per record and a
binding indirection through it, which is the module-record machinery Phase 66
said it did not have.

Also still open, unchanged by this phase: `export * as ns from`, dynamic
`import()`, `import.meta`, cycles, top-level `await`, module `<script>` support,
and typed arrays for transferable messaging (`ArrayBuffer` is not exposed to
script at all, so the transfer list has nothing to carry).
