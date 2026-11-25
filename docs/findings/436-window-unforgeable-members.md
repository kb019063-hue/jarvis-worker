# Finding 436 — Window [LegacyUnforgeable] members own-accessor fix

## The defect
The Window's four `[LegacyUnforgeable]` members (`window`, `document`, `location`, `top`) were either own data properties (configurable) or prototype-only accessors. This allowed pages to forge them (`delete globalThis.window` answered `true`) and violated WebIDL §3.4.10 and §3.7.6.

## The fix
- **Env-binding permanence**: `mark_permanent` for all four names in `JsInterpreter::init_language_builtins`.
- **Own accessor copies**: Copy prototype getter/setter into instance object with `Enumerable|Accessor` flags (no `Configurable`) in `DomWindowBindings::attach_window`.

## Verification
- Node v22.22.3 oracle matches: non-configurable own accessor, `delete` false, `var top = 5` keeps accessor, `defineProperty` TypeError.
- Red-proved in three ways (unfixed, mark-only, accessor-only) and green on full fix (493/493 suites).

## Phase and Finding
- Phase: #436
- Finding: #436

## Agent
- Merlin
