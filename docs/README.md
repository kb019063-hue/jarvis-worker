# Where a write-up goes

`ARCHITECTURE.md` describes the engine **as it is**. `AUDIT.md` holds the
findings that are **still open**. Neither is a log of work.

They became one. Over the first 60 phases `ARCHITECTURE.md` grew to 435 KB, of
which **86% (374 KB, 46 sections) is phase write-ups**, and `AUDIT.md` to
296 KB, of which **56 of 69 findings are resolved**. An agent claiming a task
has to find two integers in all that — the next free phase number and the next
free finding number — which meant reading 731 KB of markdown to extract two
numbers, and still colliding, because two agents that looked at the same moment
took the same one.

So a write-up goes here instead, and the number comes from
`python3 tools/next_ids.py`.

## A phase write-up

**`docs/phases/NN-short-title.md`** — `NN` the phase number, then a hyphen,
then a few lower-case words separated by hyphens. One file per phase.

```
docs/phases/56-cross-origin-window-reflection.md
docs/phases/60-unicode-normalize-and-collation.md
```

## A finding

**`docs/findings/NN-short-title.md`** — the same naming. `AUDIT.md` keeps the
open findings and a one-line pointer to each file here.

## What goes in one

The same four things `AGENTS.md` asks for, in the same order:

1. what was wrong,
2. what changed,
3. how it was verified — **a command anyone can run, and the number it prints**,
4. what is still open.

## Why files and not sections

Two agents writing at once each add a file and never touch the other's. A
section has to be appended to a file both of them are editing, which is why
`AGENTS.md` says *"do not re-wrap or reorder what is there"* — and that rule is
why the phase sections already in `ARCHITECTURE.md` are out of order (36 sits
before 34, and 49 and 50 before 46 and 48). New files do not have that
problem, and they can be moved, sorted and renamed without a merge conflict.

## What happened to the sections that were there

They moved here, on 2026-11-17: 46 phase write-ups out of `ARCHITECTURE.md`
(435 KB → 71 KB) and 56 resolved findings out of `AUDIT.md` (296 KB → 38 KB).
Nothing was deleted and nothing was rewritten — each file is its section, in
the same order, and every heading the two documents had is still somewhere in
the repository. What is left in them stays exactly where it is: do not edit,
reorder or delete it in passing.

Each document ends with an index of what moved out of it. The number in a
filename here is what counts: the index is for a reader, and
`tools/next_ids.py` reads the filenames.

## The second file here

`state.json` holds the durable floors for the two counters. It says what it is
inside.
