# Phase 436 — Window [LegacyUnforgeable] members are own, non-configurable, enumerable accessors

## Summary
The Window's four `[LegacyUnforgeable]` members (`window`, `document`, `location`, `top`) are now own, non-configurable, enumerable accessors on the window object (WebIDL §3.4.10 + §3.7.6 + HTML §7.2). This replaces the disproved "stale path" theory and ensures `delete globalThis.window` is `false` and nothing can be forged.

## Implementation (Two Independent Halves)
1. **Env-binding permanence (`src/js/JsInterpreter.cpp`)**:
   - Added `mark_permanent` block for `window`, `document`, `location`, `top` at lines 1238-1250.
   - Ensures `[[Delete]]` answers `false` for these bindings (Environment Record hook).
   - `self` and `globalThis` are deliberately left configurable.

2. **Own accessor copies (`src/js/DomWindowBindings.cpp`)**:
   - Inserted own-accessor copy loop in `attach_window` (lines 2817-2846) before `js_uncaught_handler`.
   - Copies prototype entry's getter/setter with flags `Enumerable|Accessor` (no `Configurable`).
   - Ensures descriptor shape: own, non-configurable, enumerable, accessor.

## Test Suite Adjustments (`tests/test_suite.cpp`)
- Renamed local variables shadowing `top` in `computed_style`, `ranges`, `dom_trees`, and `eat-el` blocks to `root` or `head` to avoid collisions with the now non-configurable own accessor.

## New Test Suite (`tests/suites/436-window-unforgeable-members.cpp` → `tmp-unforgeable.cpp`)
- **`the_window_unforgeable_members_are_own_and_non_configurable`**:
  - Verifies descriptor shape (own, non-configurable, enumerable, accessor).
  - Allows `location` setter (`[PutForwards=href]`), enforces readonly for others.
  - Verifies ownership, prototype preservation, enumeration (`Object.keys`/`getOwnPropertyNames`), `delete` false, `defineProperty` TypeError, and real values.
- **`the_window_members_that_are_not_unforgeable_stay_configurable`**:
  - Verifies `self` and `globalThis` remain deletable.
  - Verifies `var top = 5` keeps the accessor value (Node-verified).

## Verification
- **Red Proofs**:
  - **Unfixed Engine (Main)**: Failed at `:52` (assertion `bad.join(',') || 'ok' == "ok"`).
  - **Plausible-but-Wrong Fix 1 (Marks only)**: Failed at `:52` (`location`/`top` prototype-only).
  - **Plausible-but-Wrong Fix 2 (Accessors only, configurable)**: Failed at `:52` (`[[Delete]]` answered `true`).
  - **Full Fix (Both halves)**: **PASSED** (All 493 engine test suites passed successfully).
- **Node-Verification**: All expected values match Node v22.22.3 (non-configurable own accessor, `delete` false, `var top = 5` keeps accessor, `defineProperty` TypeError).

## Phase and Finding
- Phase: #436
- Finding: #436

## Agent
- Merlin
