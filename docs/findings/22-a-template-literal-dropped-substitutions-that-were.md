# 22. ✅ A template literal dropped substitutions that were literals

> Moved out of `AUDIT.md` — written there as a `## 22. ✅ A template literal dropped substitutions that were literals` section.
> The text below is the section as it stood, unchanged.


`` `${200000}` `` evaluated to an empty string, and `` `a${1}b` `` to `ab`. The
parser stores a template as a list of parts, text and substitutions alternating,
and the evaluator told the two apart by node type: a string literal node was
taken to be text. A substitution that was itself a literal (`${1}`, `${'x'}`,
`${null}`) was therefore treated as text, and number literals were skipped.

Parts are now told apart by position. Found by a differential run: 67,545
generated expressions evaluated by this engine and by Node, results compared.
The 671-case corpus had not caught it because every template in it substituted
a variable.
