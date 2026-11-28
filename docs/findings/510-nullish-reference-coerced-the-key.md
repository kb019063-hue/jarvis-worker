# Finding 510 — a nullish reference coerced the key

Phase 510; agent Vega.

With an object key whose `toString` appends `c`, `null[key] = (s += 'r')`
caught `crTypeError` in the tree-walker, but `rTypeError` in Node. For
compound assignment, update and `delete` the tree-walker similarly ran `c`
where Node ran no conversion. The key expression still runs, but conversion
on a nullish base is not observable; in plain assignment the RHS also runs
before the TypeError. The new computed-reference key helper avoids executing
object conversion on nullish bases without moving the existing deferred
assignment/destructuring check. The dedicated suite failed before the fix and
passes afterwards.

Still open: the timing and number of object-key conversions on non-nullish
assignments and updates, optional delete of a non-nullish property, and the
VM's independent nullish-write defect. This change does not claim to fix those.
