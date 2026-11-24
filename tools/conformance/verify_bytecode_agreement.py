#!/usr/bin/env python3
"""Checks that the bytecode VM answers what the tree-walker answers.

Why this exists
---------------
ROADMAP row 7 asks for the bytecode VM to become the engine, with the
tree-walker kept as a **differential oracle**: two independent implementations
of the same standard, run on the same script, required to agree. That plan has
a prerequisite nobody had built -- a way to run both and compare -- and without
it the VM was free to rot. It did:

    $ ./build/litebird --run-js 'var x = 5; x * 2' --dump-bytecode
    Cannot compile to bytecode: Function-scoped var is not supported ...

The compiler refused `var`, so it could not compile any ordinary script, and
`grep -rn eval_bytecode tests/ tools/` found nothing at all: no test, no
checker, no caller. A subsystem nothing calls cannot fail, and so it did not.

So this harness is the first half of the work rather than a side effect of it.
It runs every case through `tools/conformance/bytecode_probe`, which evaluates
the same script twice -- once in `JsInterpreter::eval` and once in
`JsInterpreter::eval_bytecode` -- and requires the two answers to be equal.
Equality is on the answer as JavaScript sees it (`String(value)`), because that
is the only thing both paths can be asked for without a shared object graph.

The corpus is split in two, deliberately:

  * ``agrees`` -- cases both paths must answer alike. A failure here is a
    defect in one of them, and the case does not go in this list until the two
    agree for a reason that has been read.
  * ``refuses`` -- cases the **compiler** still refuses, each with the message
    it refuses with. These are not passes: they are the work remaining, kept
    visible and counted so the list shrinks and cannot quietly grow.

A case that starts agreeing while it is in ``refuses`` fails the run, the way
the pixel oracle's `FIXED` does: the corpus shrinks rather than accumulating
excuses.

    python3 tools/conformance/verify_bytecode_agreement.py build/bytecode_probe
    python3 tools/conformance/verify_bytecode_agreement.py --list
    python3 tools/conformance/verify_bytecode_agreement.py --self-test

`--self-test` does not touch the engine: it checks this script's own reading of
the probe's output on fixed texts, so a harness that has stopped noticing a
disagreement is visible before it is trusted. `--dump` writes every answer out,
which is how a case gets moved from one list to the other.
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from dataclasses import dataclass


@dataclass(frozen=True)
class Case:
    """One script, and what the two paths must do with it."""

    source: str
    note: str


# Both paths must answer these alike. Each was run on the engine and read
# before it was written here; none of them is a guess.
AGREES: list[Case] = [
    Case("1 + 2", "a constant expression"),
    Case("'a' + 'b'", "string concatenation"),
    Case("7 * 6 - 2", "left-to-right precedence"),
    Case("[1,2,3].length", "an array literal and a property read"),
    Case("String(1 < 2) + String(2 <= 2) + String(1 !== 1)", "comparisons"),
    Case("function f(a) { return a + 1; } String(f(41))", "a function call"),

    # `var`: the compiler refused it outright, which made every ordinary
    # script uncompilable. It is a binding of the enclosing function, and at
    # the top level that function is the script, whose bindings are the global
    # object's.
    Case("var a = 1; String(a)", "a top-level var"),
    Case("var a = 1, b = 2; String(a + b)", "two declarators in one statement"),
    Case("var a = 1; { var b = 2; } String(a + b)",
         "a var nested in a block outlives the block"),
    Case("var x = 1; x += 4; String(x)", "a compound assignment to a var"),
    Case("function f() { var t = 7; return t; } String(f())",
         "a var in a function body is that function's, not the script's"),
    Case("function g() { var t = 0; for (var w = 0; w < 4; w++) t = t + w; return t; } String(g())",
         "a var inside a for inside a function"),

    # The ternary, which was refused as an unsupported expression.
    Case("true ? 'y' : 'n'", "the consequent is taken"),
    Case("1 > 2 ? 'y' : 'n'", "the alternate is taken"),

    # `do`/`while`, which was refused as an unsupported statement.
    Case("var e = 0; do { e = e + 1; } while (e < 2); String(e)", "a do loop round twice"),
    Case("var k = 0; do { k = k + 1; } while (false); String(k)",
         "a do loop runs its body once even when the test is false"),

    # `break` and `continue`, which were refused.
    Case("var m = 0; while (true) { m++; if (m > 3) break; } String(m)",
         "break out of a while whose test never fails"),
    Case("var t = 0; for (var i = 0; i < 10; i++) { if (i > 4) break; t += i; } String(t)",
         "break out of a for"),
    Case("var z = 0; do { z = z + 1; if (z > 2) break; } while (z < 10); String(z)",
         "break out of a do loop"),
    Case("var u = 0; for (var i = 0; i < 5; i++) { if (i == 2) continue; u += i; } String(u)",
         "continue in a for runs the update"),
    Case("var v = 0; for (var i = 0; i < 4; i++) { continue; v += 100; } String(v)",
         "continue as the whole body still advances the loop"),
    Case("var w = 0; var j = 0; while (j < 5) { j++; if (j == 3) continue; w += j; } String(w)",
         "continue in a while goes back to the test"),
    Case("var z = 0; var q = 0; do { q++; if (q == 2) continue; z += q; } while (q < 4); String(z)",
         "continue in a do loop goes to the test"),
    Case("String((function(){ var r = 0; for (var i = 0; i < 3; i++) "
         "{ for (var j = 0; j < 3; j++) { if (j == 1) continue; r++; } } return r; })())",
         "continue in an inner loop does not leave the outer one"),

    # Nested loops and the arithmetic that carries them.
    Case("var t = 0; for (var u = 0; u < 3; u++) { for (var v = 0; v < 3; v++) { t++; } } String(t)",
         "an inner loop runs to its end each time"),
    Case("function h(n) { var acc = 1; for (var i = 2; i <= n; i++) acc *= n; return acc; } String(h(5))",
         "a loop that multiplies"),
    Case("var arr = [1,2,3]; var n = 0; for (var i = 0; i < arr.length; i++) { n = n + arr[i]; } String(n)",
         "indexing an array in a loop"),
    Case("let s = 0; for (let z = 0; z < 4; z++) { s += z; } String(s)",
         "a let loop counter"),

    # `typeof`, the one unary operator the compiler refused. The VM answers
    # with the tree-walker's own `js_typeof_value`, so the two cannot drift;
    # an undeclared global is `undefined` on both paths, which is the one
    # place `typeof` must not throw (ECMA-262 §13.5.3).
    Case("String(typeof 1)", "typeof a number"),
    Case("String(typeof 'x') + String(typeof true) + String(typeof undefined)",
         "typeof a string, a boolean and an undefined"),
    Case("String(typeof null) + ' ' + String(typeof Symbol('s')) + ' ' + String(typeof Symbol)",
         "typeof null is object; typeof a symbol and of the Symbol function"),
    Case("String(typeof neverDeclaredAnywhere)",
         "typeof an unresolvable reference is undefined, not a ReferenceError"),
    Case("function f() { return 1; } String(typeof f) + String(typeof f())",
         "typeof a function and of its return"),
]

# The compiler refuses these. Kept here so the count of what is left has a
# number, and so the day one of them starts working shows up as a failure
# rather than as silence.
REFUSES: list[Case] = [
    Case("var [a, b] = [1, 2]; String(a)", "Destructuring"),
    Case("switch (1) { case 1: break; }", "Switch"),
    Case("try { 1; } catch (e) { 2; }", "Statement"),
    Case("var s = \"\"; for (var k of [1,2]) { s += k; } String(s)", "Statement"),
    Case("var o = {a: 1, ...{b: 2}}; String(o.b)", "object literal form"),
    Case("function f() { return 1; } String(f`x`)", "expression"),
]


def run_probe(probe: str, source: str) -> dict[str, str]:
    """The two answers, as {'ast': ..., 'vm': ...}, each 'ok:<value>' or 'err:<message>'."""
    finished = subprocess.run(
        [probe, "-e", source],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    answers: dict[str, str] = {}
    for line in finished.stdout.splitlines():
        read = parse_line(line)
        if read is not None:
            answers[read[0]] = read[1]
    if finished.returncode != 0 and not answers:
        raise RuntimeError(f"the probe exited {finished.returncode}: {finished.stderr.strip()}")
    return answers


def is_refusal(answer: str) -> bool:
    """True when the message is the bytecode compiler declining to compile.

    These are the words `BytecodeCompiler` refuses in. Anything else that
    comes out of the VM is a disagreement to be looked at, not a refusal to be
    counted, so the list is explicit rather than a substring guess.
    """
    text = answer[len("err:"):] if answer.startswith("err:") else answer
    return any(
        marker in text
        for marker in (
            "not supported by the bytecode compiler",
            "in bytecode compiler",
            "via AST interpreter",
            "Function-scoped var",
            "Destructuring",
        )
    )


def described(answer: str) -> str:
    """'err:Function-scoped var is not supported...' -> 'refused: Function-scoped var ...'."""
    if answer.startswith("ok:"):
        return answer[len("ok:"):]
    return "threw: " + answer[len("err:"):]


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("probe", nargs="?", help="the bytecode_probe binary")
    parser.add_argument("--list", action="store_true", help="print every case and exit")
    parser.add_argument("--dump", action="store_true", help="print every answer, pass or fail")
    parser.add_argument("--self-test", action="store_true", help="check this script's reading only")
    args = parser.parse_args(argv[1:])

    if args.self_test:
        # The reading of the probe's output, on fixed texts. The failure this
        # guards against is a harness that compares nothing and says "ok".
        lines = {
            "ast threw RangeError: Maximum call stack size exceeded": (
                "ast", "err:RangeError: Maximum call stack size exceeded"),
            "vm value 42": ("vm", "ok:42"),
            "ast value ": ("ast", "ok:"),
            "vm threw Function-scoped var is not supported": (
                "vm", "err:Function-scoped var is not supported"),
            # The bug this form exists to stop: a message that carries the other
            # side's word must not be read as that side's answer.
            "vm threw the ast value 99 was wrong": ("vm", "err:the ast value 99 was wrong"),
            "nonsense": None,
        }
        for line, expected in lines.items():
            got = parse_line(line)
            if got != expected:
                print(f"self-test: {line!r} read as {got!r}, expected {expected!r}")
                return 1
        print(f"verify_bytecode_agreement: self-test ok ({len(lines)} readings)")
        return 0

    if args.list:
        for case in AGREES:
            print(f"agrees  {case.source:60s}  {case.note}")
        for case in REFUSES:
            print(f"refuses {case.source:60s}  {case.note}")
        print(f"\n{len(AGREES)} must agree, {len(REFUSES)} still refused")
        return 0

    probe = args.probe or shutil.which("bytecode_probe")
    if not probe:
        print("verify_bytecode_agreement: no probe binary (pass one, or build it)", file=sys.stderr)
        return 2

    failures = 0
    print(f"agreement: {len(AGREES)} case(s) both paths must answer alike")
    for case in AGREES:
        answers = run_probe(probe, case.source)
        ast, vm = answers.get("ast", "err:no answer"), answers.get("vm", "err:no answer")
        if args.dump:
            print(f"  {case.source:58s} ast={described(ast):24s} vm={described(vm)}")
        if ast != vm:
            failures += 1
            print(f"  FAIL  {case.source}")
            print(f"        the tree-walker answers {described(ast)}")
            print(f"        the bytecode VM answers {described(vm)}")
    print(f"  {len(AGREES) - failures}/{len(AGREES)} agree")

    # The refusals are the work that is left, not a pass. Each line says what is
    # refused; a case that has started agreeing is reported so the list shrinks.
    still_refused = 0
    fixed: list[Case] = []
    for case in REFUSES:
        answers = run_probe(probe, case.source)
        ast, vm = answers.get("ast", "err:no answer"), answers.get("vm", "err:no answer")
        # A refusal is the *compiler* saying so, which it does in one of a few
        # fixed ways. Matching on "bytecode compiler" alone missed the switch,
        # whose message is "Switch compiled via AST interpreter", and the run
        # then read a refusal as a disagreement. The forms are listed rather
        # than guessed, and a message that is none of them is a disagreement --
        # which is the safe way round.
        if vm.startswith("err:") and is_refusal(vm):
            still_refused += 1
            if args.dump:
                print(f"  refused  {case.source:56s} {described(vm)}")
            continue
        fixed.append(case)
        if ast == vm:
            print(f"  AGREES NOW  {case.source}")
            print(f"        {case.note} was refused and now answers {described(vm)};")
            print(f"        move the case to the agreeing list")
        else:
            print(f"  FAIL  {case.source} -- the VM compiles it and answers {described(vm)},")
            print(f"        where the tree-walker answers {described(ast)}")

    print(f"  {still_refused}/{len(REFUSES)} still refused, {len(fixed)} no longer refused")

    if failures or fixed:
        print(f"\nverify_bytecode_agreement: {failures + len(fixed)} failure(s)")
        return 1
    print("\nverify_bytecode_agreement: 0 failure(s)")
    return 0


def parse_line(line: str) -> tuple[str, str] | None:
    """What `run_probe` makes of one line of the probe's output.

    The form is `<side> <status> <text>`: side first, status always the second
    word. An earlier form put the side second, and a compiler message that
    itself began "vm " was then read as a second answer, so a refusal could be
    counted as an agreement. The status being a fixed second word is what makes
    that impossible rather than unlikely.
    """
    parts = line.split(" ", 2)
    if len(parts) < 2 or parts[0] not in ("ast", "vm"):
        return None
    side, status = parts[0], parts[1]
    text = parts[2] if len(parts) > 2 else ""
    if status == "value":
        return (side, "ok:" + text)
    if status == "threw":
        return (side, "err:" + text)
    return None


if __name__ == "__main__":
    sys.exit(main(sys.argv))
