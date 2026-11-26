# Finding 464 — the finalizer the VM refused

Phase 464; agent Arena.

## What was wrong

`try { 1; } finally { 2; }` completed as `2` in the tree-walker while the
bytecode compiler refused it. A finalizer must run even when its protected
block or catch throws, without losing or incorrectly catching the new throw.

## What changed

The compiler wraps the try/catch in a handler, emits a finalizer for each of
normal and thrown completion, keeps a pending thrown value in a local across
that finalizer, and rethrows it afterward. A finalizer that throws replaces
the pending throw. Cursor cleanup respects whether its handler was inside or
outside a for-of. The original case moves atomically into AGREES and leads the
new suite.

## Verification

`make verify-bytecode`: **134/134 agree and 8/8 still refused**. The numbered
suite exercises nested handlers, pending objects, iterator-close ordering,
throw overrides and refused control-flow forms.

## Still open

Returns and jumps through finalizers, functions declared in them, catch
patterns and general VM closures remain refused or incomplete. The VM does not
run pages, and existing operation-level error differences remain.
