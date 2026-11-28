# Phase 514 — VM nullish computed writes do not convert the key

Finding: docs/findings/514-vm-nullish-computed-writes.md

Phase 510 fixed the tree-walker so nullish computed writes never convert an object key. The VM OpSetIndex still did.
