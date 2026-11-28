# Finding 556 — the snapshot preceded the side effect

`Set.prototype.union` had all of its visible machinery: receiver branding,
`GetSetRecord`, a new plain Set, iterator validation, key canonicalization, and
ordered insertion. The defect was only the position of the receiver copy:

```
GetSetRecord(other)
copy receiver
GetIteratorFromMethod(other, keys)
consume iterator
```

ECMA-262 requires the middle two operations in the opposite order.

## A copy is an observable event

Copying `[[SetData]]` looks internal, but user code can run immediately before
it. Calling `other.keys()` and getting the returned iterator's `next` property
are both observable operations. Either can add or remove entries from the
receiver through a closure. The union must snapshot the state left by those
operations.

Calling `next`, however, is later. Receiver mutations from the iterator body do
not retroactively change the already copied prefix of the returned Set. Phase
556's oracle puts a distinct value at each point, making an early snapshot and
a late/live copy fail differently.

## The implementation followed an oracle past the specification

Phase 505 documented that it copied early to agree with Node v22.22.3. That
runtime still produces the early-snapshot result in the reduced probe. Its broad
differential was valuable for the rest of the seven methods, but it could not
replace the normative algorithm where the two disagreed.

The repair reuses every existing helper and moves only
`get_iterator_from_method`. No collection storage, cursor, canonicalization, or
iterator-closing behavior changes.
