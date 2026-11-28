# Finding 530 — optional delete left the property in place

Phase 530; agent Vega.

`delete o?.x` answered true but left `o.x` intact: the `delete` evaluator saw
an optional-chain wrapper as a value expression, read the property and threw
away the resulting value. This even invoked a getter. It now keeps the final
member or index as a property reference, deletes it when the chain reaches
it, and returns true without evaluating subsequent keys or links when it
short-circuits. Strict refusal and a chain ending in a call keep their usual
semantics. The regression suite failed before the fix and checks 20 traces
against Node.

Non-nullish computed-assignment conversion order and VM nullish writes remain
separate defects; this finding does not claim they are fixed.
