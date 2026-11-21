# Finding 164 — the hint was already being ignored

## What was seen

`Object.getOwnPropertyDescriptor({ get id() {} }, "id").get.name` printed
`"id"`. The source of the fix looked obvious: the definition site passes the
key as a name hint, so pass `"get " + key` instead.

That change did nothing. The name was still `"id"`.

## Why

`make_function(node, env, name_hint)` uses the hint only when the function node
has no name of its own:

```cpp
fd.name = node->name.empty() ? name_hint : node->name;
```

and the parser gives every method and every accessor its key as a name at the
moment it parses it (`fn->name = member.key;`), because that is right for the
ordinary case. So every definition site had been passing a hint into a function
that ignored it, and the broken output was identical to the working output for
every case anyone had looked at.

The general shape: **a parameter that is silently unused is indistinguishable
from a used one** as long as the caller's value happens to equal the value the
callee supplied itself. It took a case where the two differ — a symbol key, an
accessor's prefix — to see that the channel was not connected at all.

## What made it visible

Not reading harder. Printing the name from the engine and from Node side by
side, for every form of definition that exists:

```
obj fn:     "id" "@@sym:1" "@@sym:2" "x"        <- and Node: "id" "" "[test262]" "x"
accessors:  "id" "get [@@sym:1]" ...            <- and Node: "get id" "get " ...
class:      "m" "s" "g" "g"                     <- and Node: "m" "s" "get g" "set g"
```

The `@@sym:1` was the second tell. A symbol's *internal* key is what
`js_to_property_key` returns, and it had been flowing into the human-visible
`name` — the two forms of a property key are not interchangeable, and a hint
that is a string cannot carry a symbol at all. That is why the fix keeps the
evaluated key beside its string form rather than trying to reconstruct one from
the other.

## The rule

When a correction "does nothing", check that the value arrives before changing
what the value is. Here the value never arrived, and the same edit made three
times would have looked like three separate bugs.
