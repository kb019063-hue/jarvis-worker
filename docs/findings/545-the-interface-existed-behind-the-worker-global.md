# Finding 545 — the interface existed behind the worker global

ImageData was already a complete realm interface. Its constructor accepted both
size and typed-array overloads, its accessors were branded, and an
OffscreenCanvas in a worker already returned working ImageData instances. Yet a
worker script could not write `new ImageData(...)` because the worker-global
installation list stopped after OffscreenCanvas and Path2D.

## Reachability is part of an interface

Registering an interface gives `Realm::ctor("ImageData")` a constructor; it does
not decide which global scopes expose that value. Window attachment handles page
realms. Non-document interpreters have an explicit list because they must not
inherit Window-only DOM APIs.

The right fix was therefore not in ImageData's constructor and not in its canvas
conversion. It was to add this worker-safe interface to that explicit list,
using the constructor from the worker's own realm.

## This engine has two worker-global surfaces

A bare worker name is resolved through the interpreter's global environment.
The engine's `self`, however, is a separately allocated host object populated by
worker setup. That design requires worker APIs to be installed twice with the
same value:

```
ImageData          -> non-document interpreter environment
self.ImageData     -> the identical value read from that environment
```

A mutation that removes only the second line leaves construction and the full
pixel round trip working. It still violates the observable worker-global API:
`self.ImageData` is absent. The suite checks identity rather than presence alone
so two accidentally duplicated constructors would fail as well.

## Pixels prove more than exposure

A `typeof` check could pass with a stub or a constructor from the wrong realm.
Phase 545 constructs a four-colour typed-array image, writes it through
`putImageData()`, and reads every RGBA byte back through `getImageData()`. The
returned object and both typed arrays are checked against worker-realm
constructors. That joins global exposure to the existing interface, overload,
branding, and canvas paths without adding a second implementation.
