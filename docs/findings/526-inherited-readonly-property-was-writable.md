# Finding 526: An inherited read-only property was written over

`JsValue::set_property` refused an inherited accessor without a setter but allowed an inherited non-writable data property to be shadowed. The prototype scan was guarded by `has_accessors`, so any prototype that held only data properties was never inspected.

A read-only property through `Object.defineProperty(p,'ro',{value:1})` on a prototype `p` should prevent `Object.create(p)` from gaining an own `ro`; strict assignment should throw `TypeError`. The same holds for `class B extends A` where `A` has a non-writable static `ro`.

Fixed by removing the `has_accessors` guard and checking `PropertyMap::Writable` for data properties on every prototype. The new suite reproduces the `prototypes.dat` failure and its class variant.
