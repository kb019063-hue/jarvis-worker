# Finding 101 — A document built children a document cannot have

**Resolved by** `docs/phases/95-document-write-writes-into-the-parser.md`.

## What was wrong

The tree builder inserted a node wherever "the appropriate place for inserting
a node" pointed, and never asked whether the parent could hold it. When the
parent was a **document**, it often could not: a document has at most one
element child and no text children at all. The parser therefore built trees the
DOM itself would reject — the tree dump was the only thing that noticed, and it
noticed as a wrong answer in a corpus case rather than as an error.

It needed a script to reach: `scripted_foster01.dat` removes
`document.documentElement` and re-appends the table, so the table becomes the
document's element child, and the `<b>` (or the text) that follows the script
is foster-parented "before the table" — into the document, as a second element
child, or as a text child.

## Why it was not obvious

The insertion point was *valid* when it was computed; it stopped being valid
when the script moved the root, and the parser kept using the parent it had.
The engine's own dump then showed the extra child, which reads like a
foster-parenting bug and is not one: the foster parenting pointed at the right
parent, and the parent refused the node.

## What a reader should take away

Any insertion the parser makes is an insertion the DOM has rules about. Where
those rules are checked in `Node::append_child` for script, the parser does not
go through them, so the two questions — "where does the spec say to put it" and
"may it go there" — have to be asked separately. The second one has an answer
in a corpus case (`scripted_foster01.dat`), which is the cheapest place to
notice it.
