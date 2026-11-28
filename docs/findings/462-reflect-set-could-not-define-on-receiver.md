# Finding 462 — Reflect.set could not define a data property on its receiver

## Summary

`Reflect.set(target, key, value, receiver)` used `target.set_property(key, value)`. A thread-local scope made an accessor setter use `receiver` as `this`, but ordinary writes were still performed on `target`. Thus, when the target or its prototype chain had a writable data descriptor, an existing receiver data property was not updated, and an absent receiver property was not created there. This was the still-open case recorded in Phase 414.

## Root cause

`JsValue::set_property` implements a property's write against the object it is called on. Its setter scope only changed the `this` value of accessor callbacks; it did not implement §10.1.9's `OrdinarySetWithOwnDescriptor` receiver steps. Consequently, data writes bypassed the receiver's own descriptor and extensibility checks, and a setter's nested writes could also observe the thread-local receiver while the scope remained active.

## Fix

`Reflect.set` now uses a receiver-aware path whenever the fourth argument is supplied and is not the target. The path resolves the target's own or inherited descriptor first: accessors invoke their setter with the receiver, non-writable data properties refuse, and writable data properties update a writable own data property on the receiver or create one with default attributes. Receiver property creation does not run inherited setters. The omitted-argument case still uses the target; explicit `undefined` remains a distinct primitive receiver.

`JsValue::create_data_property` performs the own definition directly, including array index storage and length validation. Descriptor lookup and `has_own_property` now account for sparse array index entries, allowing an existing sparse receiver property to be recognized. If no ordinary descriptor resolves, a host exotic's existing `[[Set]]` hook retains its established behavior; valid typed-array indices use their writable descriptor and write on the receiver.

## Verification

- `make test`: 508/508 engine suites and 114/114 cross-origin Window/Location and MessageChannel checks pass.
- `make js-conformance`: 1,153/1,158; 0 crashes and 0 timeouts.
- `tests/suites/462-reflect-set-defines-on-receiver.cpp`: 20 Node v22.22.3-derived assertions, including ordinary/inherited descriptors, setters, receiver refusal rules, dense and sparse array indices, typed-array indices, and symbols.

## Still open

The engine has no `Proxy`, so Proxy `set` trap semantics remain unimplemented. Other host-specific `[[Set]]` algorithms are preserved through their existing hooks and are not exhaustively re-specified here.
