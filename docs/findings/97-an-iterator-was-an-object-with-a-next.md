# Finding 97 — An iterator was an object with a `next`, and its kind was lost

**Resolved by** `docs/phases/91-an-iterator-is-a-kind.md`.

## What was wrong

Everything the language says about an iterator object's *shape* was absent:
`next` was a property of the iterator itself rather than of a prototype its
kind shares, every kind of iterator had the same prototype
(`%IteratorPrototype%`, which had nothing on it but `@@iterator`), and no kind
had a name, so `Object.prototype.toString.call([][Symbol.iterator]())` was
`[object Object]`. Two pairs of built-ins that the language makes one function
were two functions: `Map.prototype[Symbol.iterator]` and
`Map.prototype.entries`, and `Set`'s `values`/`keys`/`@@iterator`.

## Why it mattered, beyond `Object.getPrototypeOf`

The iteration fast paths read an array's elements, or a string's code points,
without making an iterator, on the stated grounds that nothing a script could
reach stood between the installed method and the elements. Once `next` belongs
to a prototype, that is false: a script may replace it, and a replacement that
yields `9` forever would be ignored by every `for…of`, spread and `Array.from`
in the engine. The phase therefore had to change the fast path in the same
commit that moved `next`, or the fix would have been the bug.

## How it was found

`iteration.dat` records what a reference engine prints for both questions, and
two of its eleven failing cases are exactly them:

```
iteration.dat: Array.prototype[Symbol.iterator] is Array.prototype.values, and the others likewise
iteration.dat: array iterators take next from one prototype
```

Neither could pass while property keys are strings: the case that asks for
`'Symbol.iterator' in []` to be `false` still cannot (see the open rows).

## What a reader should take away

A built-in that is registered by three calls is three functions. Where the
language says two names are one function object, the second name has to be
given the first one's value, not a second implementation that agrees with it —
a script can tell `f === g` apart from `f() === g()`, and `f.name` from the
name the caller happened to use.
