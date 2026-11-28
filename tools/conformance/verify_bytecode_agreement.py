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
    # Phase 540: a catch return runs its surrounding finalizers.
    Case("function f(){try{throw 1}catch(e){return e}finally{}}f()", "original catch return through finalizer"),
    Case("var s='';function f(){try{throw 3}catch(e){s+='c';return e}finally{s+='f'}}String(f()+'/'+s)", "catch and finalizer order"),
    Case("var s='';function f(){try{throw 2}catch(e){try{return e}finally{s+='i'}}finally{s+='o'}}String(f()+'/'+s)", "return through nested finalizers from catch"),
    Case("var s='';function f(){try{throw 2}catch(e){try{return e}catch(x){s+='c'}finally{s+='i'}}finally{s+='o'}}String(f()+'/'+s)", "nested catch handler is skipped"),
    Case("var s='';function f(){try{throw 2}catch(e){return missing_name}finally{s+='f'}}try{f()}catch(e){s+=e.name}String(s)", "throw in return expression runs finalizer"),
    Case("var s='';function f(){try{throw 2}catch(e){return e}finally{s+='f';throw 7}}try{f()}catch(e){s+=e}String(s)", "finalizer throw replaces return"),
    Case("var e=8,s='';function f(){try{throw 2}catch(e){return e}finally{s+=e}}String(f()+'/'+s)", "catch binding is out of scope in outer finalizer"),
    Case("var s='';function f(){try{throw 2}catch(e){try{throw 4}catch(e){return e}finally{s+=e}}finally{s+='o'}}String(f()+'/'+s)", "nested catch shadows and restores binding"),
    Case("var s='';function f(){try{try{throw 2}catch(e){return e}finally{s+='i';throw 7}}catch(e){s+=e;return e+1}finally{s+='o'}}String(f()+'/'+s)", "outer catch receives inner finalizer throw"),
    Case("function f(){try{throw {x:3}}catch(e){return e}finally{gc()}}String(f().x)", "caught return object stays rooted"),
    Case("var s='';function f(){try{throw 2}catch(e){try{return e}catch(x){s+='c'}finally{e=9;s+=e}}finally{s+='o'}}String(f()+'/'+s)", "inner finalizer sees enclosing catch binding"),
    Case("function f(){try{throw 2}catch{return;}finally{gc()}}String(f())", "optional catch binding and undefined return"),
    Case("var s='';function f(){try{throw 2}catch(e){return e}finally{s+='f'}}var o={};Object.defineProperty(o,'x',{get:f});String(o.x+'/'+s)", "return from reentrant getter"),
    Case("var s='';function f(){try{throw 1}catch(e){try{return e}finally{try{throw 3}catch(e){s+=e}}}finally{s+='o'}}String(f()+'/'+s)", "finalizer handles its own throw"),
    Case("var s='';function f(){try{try{throw 2}catch(e){return missing_name}finally{s+='i'}}catch(e){s+=e.name;return 7}finally{s+='o'}}String(f()+'/'+s)", "outer catch handles return expression throw"),
    Case("var s='';function f(){try{throw 1}catch(e){try{try{throw 3}catch(x){return x}finally{s+='a'}}finally{s+='b'}}finally{s+='c'}}String(f()+'/'+s)", "three finalizers run inside out"),
    Case("var s='';function f(){try{throw 1}catch(e){let x={v:e};return x}finally{gc()}}var a=f();String(a.v+'/'+s)", "catch local compacted while pending result is rooted"),
    Case("var s='';function f(){try{throw 5}catch(e){if(e==5)return e;return 7}finally{s+='f'}}String(f()+'/'+s)", "compiler state restored for branch returns"),
    Case("var s='';function f(){try{throw 1}catch(e){try{return missing_name}catch(x){s+=x.name}finally{s+='i'}}finally{s+='o'}return 9}String(f()+'/'+s)", "inner catch handles return expression throw"),

    # Phase 524: returns unwind catch and finally handlers in order.
    Case("function f(){try{try{return 1}catch(e){}finally{}}finally{}}f()", "original return across nested catch and finally"),
    Case("function f(){try{return 1}catch(e){}} f()", "return from catch-protected block"),
    Case("var s='';function f(){try{try{return 4}catch(e){s+='c'}finally{s+='i'}}finally{s+='o'}}String(f()+'/'+s)", "return skips catch and runs both finalizers"),
    Case("var s='';function f(){try{try{return missing_name}catch(e){s+=e.name}finally{s+='i'}}finally{s+='o'}return 9}String(f()+'/'+s)", "return expression throws into active catch"),
    Case("var s='';function f(){try{try{try{return 1}finally{s+='i';throw 7}}catch(e){s+=e}finally{s+='o'}}finally{s+='p'}return 9}String(f()+'/'+s)", "outer catch handles inner finalizer throw"),
    Case("var s='';function f(){try{try{return 1}catch(e){s+='c'}finally{s+='i';throw 7}}catch(e){s+=e}finally{s+='o'}return 9}String(f()+'/'+s)", "same try catch does not catch its finalizer"),
    Case("var s='';function f(){try{try{return 1}catch(e){s+='c'}finally{s+='i';throw 7}}finally{s+='o'}}try{f()}catch(e){s+=e}String(s)", "outer finalizer runs before throw escapes"),
    Case("function f(){try{let a=1;try{let b=2;return {v:a+b}}catch(e){}finally{gc()}}finally{gc()}}String(f().v)", "pending value rooted across handlers and GC"),
    Case("var s='';function f(){let x=1;try{let x=2;try{let x=3;return x}catch(e){}finally{s+=x}}catch(e){}finally{s+=x}}String(f()+'/'+s)", "shadowed lexical locals are trimmed at each boundary"),
    Case("var n=0;function f(v){try{if(v)return n++;return 5}catch(e){n=8}finally{n++}}String(f(true)+'/'+f(false)+'/'+n)", "conditional returns evaluate before finalizer"),
    Case("var s='';function f(){try{try{return 1}finally{s+='i'}}catch(e){s+='c'}finally{s+='o'}}String(f()+'/'+s)", "outer catch stays active for inner finalizer"),
    Case("function f(){try{return;}catch{}finally{gc()}}String(f())", "optional catch binding and undefined return"),

    Case("var s='';function f(){try{return 3}catch(e){s+='c'}}try{s+=f();throw 7}catch(e){s+=e}String(s)", "no stale catch handler after return"),
    Case("var s='';function f(){try{try{return 1}catch(e){s+='c'}finally{s+='i'}}finally{s+='o'}}var o={};Object.defineProperty(o,'x',{get:f});String(o.x+'/'+s)", "return via reentrant getter"),
    Case("var s='';function f(){try{try{return 1}finally{throw 7}}catch(e){s+=e}return 9}String(f()+'/'+s)", "catch around finalizer can handle its throw"),
    Case("var s='';function f(){try{try{return 1}catch(e){s+='a'}finally{s+='i'}}catch(e){s+='b'}finally{s+='o'}}String(f()+'/'+s)", "multiple catch and finalizer handlers"),

    # Phase 504: a return runs nested finalizers from inside out.
    Case("function f(){try{try{return 1}finally{}}finally{}}f()", "the original nested-finally return refusal"),
    Case("var s='';function f(){try{try{return 4}finally{s+='i'}}finally{s+='o'}}String(f()+'/'+s)", "inner finalizer precedes outer"),
    Case("var s='';function f(){try{try{try{return 7}finally{s+='a'}}finally{s+='b'}}finally{s+='c'}}String(f()+'/'+s)", "three finalizers run inside out"),
    Case("var s='';function f(){try{let a=3;try{let b=4;return a+b}finally{s+=a}}finally{s+='o'}}String(f()+'/'+s)", "each finalizer sees its lexical scope"),
    Case("var s='';function f(){let x=1;try{let x=2;try{let x=3;return x}finally{s+=x}}finally{s+=x}}String(f()+'/'+s)", "shadowed locals unwind between finalizers"),
    Case("var s='';function f(){try{try{return 1}finally{s+='i';throw 7}}finally{s+='o'}}try{f()}catch(e){s+=e}String(s)", "outer finalizer runs after inner throw"),
    Case("var s='';function f(){try{try{return 1}finally{s+='i'}}finally{s+='o';throw 8}}try{f()}catch(e){s+=e}String(s)", "outer finalizer throw replaces return"),
    Case("var s='';function f(){try{try{return missing_name}finally{s+='i'}}finally{s+='o'}}try{f()}catch(e){s+=e.name}String(s)", "throw in return expression runs both finalizers"),
    Case("function f(){try{try{return {x:2}}finally{gc()}}finally{gc()}}String(f().x)", "return value survives GC in each finalizer"),
    Case("function f(){var x={v:1};try{try{return x}finally{x.v=2}}finally{x.v+=3}}String(f().v)", "both finalizers can mutate returned object"),
    Case("function f(v){try{try{if(v)return 1;return 2}finally{v=3}}finally{v=4}}String(f(true)+'/'+f(false))", "branch returns survive both finalizers"),
    Case("function f(){try{try{return 1}finally{try{throw 2}catch(e){}}}finally{}}String(f())", "inner finalizer can catch its own throw"),

    # Phase 496: the tree-walker and VM both reject a nullish read before
    # coercing an object key. The key expression itself still executes.
    Case("var s='';var k={toString:function(){s+='c';return 'x'}};try{null[k]}catch(e){s+=e.name}String(s)", "nullish object key is not coerced"),
    Case("var k={toString:function(){throw 'converted'}};try{undefined[k]}catch(e){e.name}", "nullish base wins over throwing key conversion"),
    Case("var s='';var k={toString:function(){s+='c';return 'x'}};function key(){s+='k';return k}try{null[key()](s+='a')}catch(e){s+=e.name}String(s)", "key expression runs before error, call arguments do not"),
    # Phase 474: a nullish property read throws rather than producing undefined.
    Case("try { null.foo; } catch (e) { e.name; }", "null dot read throws"),
    Case("try { undefined.foo; } catch (e) { e.name; }", "undefined dot read throws"),
    Case("try { null['foo']; } catch (e) { e.name; }", "null index read throws"),
    Case("try { undefined['foo']; } catch (e) { e.message; }", "undefined index read reports the property"),
    Case("try { typeof null.foo; } catch (e) { e.name; }", "typeof does not suppress a property error"),
    Case("var s=''; try { null[(s += 'k')]; } catch(e) { s += e.name; } String(s)", "computed key expression runs before the error"),
    Case("function f(){return null.foo;} try{f()}catch(e){e.name}", "a TypeError unwinds a call"),

    # Phase 479: a simple return carries its value through a finalizer.
    Case("function f(){try{return 1}finally{}}f()", "the original return-through-finally refusal"),
    Case("var s='';function f(){try{return 1}finally{s+='f'}}String(f()+'/'+s)", "finalizer runs before return"),
    Case("function f(){var x=3;try{return x}finally{x=7}}String(f())", "return expression is evaluated first"),
    Case("function f(){try{{let a=2;let b=3;return a+b}}finally{let z=7;z}}String(f())", "return removes inner locals"),
    Case("function f(){let x=9;try{let x=3;return x}finally{x+=1}}String(f())", "finalizer sees outer scope"),
    Case("function f(){try{return {x:7}}finally{gc()}}String(f().x)", "return object stays rooted across GC"),
    Case("function f(){try{return 1}finally{throw 3}}try{f()}catch(e){e}", "finalizer throw overrides return"),
    Case("function f(){try{return missing_name}finally{throw 8}}try{f()}catch(e){e}", "finalizer throw overrides failed return expression"),
    Case("var s='';function f(){try{return missing_name}finally{s+='f'}}try{f()}catch(e){s+=e.name}String(s)", "failed return expression still runs finalizer"),
    Case("function f(x){try{if(x)return 3;return 5}finally{x=2}}String(f(true)+'/'+f(false))", "conditional return paths"),
    Case("var s='';function f(){try{return 3}finally{try{throw 5}catch(e){s+=e}}}String(f()+'/'+s)", "caught finalizer error preserves return"),
    Case("function f(){try{return;}finally{gc()}}String(f())", "empty return"),
    Case("function f(){var a={x:1};try{return a}finally{a.x=7}}String(f().x)", "returned object observes finalizer mutation"),
    Case("var n=0;function f(){try{return n++}finally{n++}}String(f()+'/'+n)", "return expression and finalizer each run once"),

    # Phase 464: the finalizer runs for normal and throwing completions.
    Case("try { 1; } finally { 2; }", "the original finally refusal"),
    Case("try{1;}finally{}", "empty finalizer preserves completion"),
    Case("var x=0;try{x=1}finally{x+=2}String(x)", "normal finalizer runs once"),
    Case("try{try{throw 7}finally{2}}catch(e){e}", "pending throw is resumed"),
    Case("try{try{throw 7}finally{throw 8}}catch(e){e}", "finalizer throw overrides pending throw"),
    Case("try{try{1}finally{throw 8}}catch(e){e}", "a finalizer can throw after normal completion"),
    Case("var s='';try{try{missing_name}finally{s+='f'}}catch(e){s+=e.name}String(s)", "finally preserves the VM's missing-global ReferenceError"),
    Case("try{try{missing_name}finally{throw 8}}catch(e){e}", "a finalizer throw replaces a missing-global ReferenceError"),
    Case("var x='';try{throw 1}catch(e){x+=e}finally{x+='f'}String(x)", "finalizer follows catch"),
    Case("try{try{throw 1}catch(e){throw 3}finally{2}}catch(e){e}", "catch throw passes through finalizer"),
    Case("try{try{throw 1}catch(e){throw 3}finally{throw 4}}catch(e){e}", "finalizer overrides catch throw"),
    Case("var x='';try{try{throw 1}finally{x+='a'}}catch(e){x+=e}finally{x+='b'}String(x)", "nested finalizers run inside out"),
    Case("try{try{throw {v:8}}finally{let a=2;gc();a;}}catch(e){e.v}", "pending object survives finalizer locals and GC"),
    Case("function f(){try{var x=1}finally{x+=2}return x}String(f())", "finalizer modifies a function local"),
    Case("try{throw 2}catch(e){e}finally{let e=4;e}", "finalizer has its own lexical scope"),
    Case("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';return {}}}};try{try{for(var x of a){throw 1}}finally{s+='f'}}catch(e){s+=e}String(s)", "iterator closes before an enclosing finalizer"),
    Case("var s='';var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';return {}}}};try{for(var x of a){try{throw 1}finally{s+='f'}}}catch(e){s+=e}String(s)", "an inner finalizer precedes iterator close"),

    # Phase 456: the protected block either completes or transfers the thrown
    # value to a fresh catch binding; no finalizer or pattern is implied.
    Case("try { 1; } catch (e) { 2; }", "the original try/catch refusal"),
    Case("try { throw 7; } catch (e) { e; }", "catch receives a thrown number"),
    Case("try { throw 3; } catch { 7; }", "optional catch binding"),
    Case("var x=7;try{throw 3}catch(x){x+=2}String(x)", "catch shadows an outer binding"),
    Case("var s='';try{throw 3}catch(e){s+=e;}String(s)", "catch effects survive the block"),
    Case("try { Object.create(42); } catch(e) { e.name; }", "native TypeError is caught"),
    Case("function f(){throw 8} try{f()}catch(e){e}", "a throw crosses a VM call frame"),
    Case("var o={};Object.defineProperty(o,'x',{get:function(){try{throw 5}catch(e){return e}}});try{o.x+2}catch(e){99}", "a reentrant callback catches within its own frame"),
    Case("var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){throw 7}}};try{for(var x of a){throw 4}}catch(e){e}", "the original throw wins over a failing iterator close"),
    Case("try{try{throw 1}catch(e){throw e+2}}catch(x){x}", "nested handler rethrows to outer handler"),
    Case("try{1;throw 2}catch(e){}", "empty catch preserves prior completion"),
    Case("try{throw 1}catch(e){e+2};", "completion survives a trailing empty statement"),
    Case("5;try{}catch(e){}", "empty try preserves earlier completion"),
    Case("try{1;while(false){}}catch(e){}", "empty loop resets script completion"),
    Case("try{throw 1}catch(e){let x=e; x+2}", "catch and block locals remain distinct"),
    Case("function f(){try{throw 3}catch(e){return e+1}}String(f())", "return from the catch"),
    Case("var n=0;try{throw 2}catch(e){n=e}try{throw 3}catch(e){n+=e}String(n)", "handlers do not leak"),
    Case("var s='';var it={};it[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){s+='r';return {}}}};try{for(var x of it){throw 'x'}}catch(e){s+=e}String(s)", "abrupt loop exit closes before catch"),
    Case("var s='';var it={};it[Symbol.iterator]=function(){return {next:function(){throw 'x'},return:function(){s+='r';return {}}}};try{for(var x of it){}}catch(e){s+=e}String(s)", "iterator step error does not close"),

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

    # `switch`, which the compiler refused outright ("Switch compiled via AST
    # interpreter"). The discriminant is evaluated once and kept on the stack;
    # each clause test is a strict equality evaluated in source order until the
    # first match (ECMA-262 14.13.4), and a plain `break` leaves the switch
    # where a `continue` would reach the enclosing loop. Clause bodies share
    # one block scope, as in the tree-walker.
    Case("switch (1) { case 1: break; }", "the original refusal, and it takes the case"),
    Case("var r = 0; switch (2) { case 1: r = 1; break; case 2: r = 2; break; default: r = 3; } String(r)",
         "a middle clause is taken"),
    Case("var r = 0; switch (9) { case 1: r = 1; break; default: r = 3; case 2: r = 2; break; } String(r)",
         "default is taken when nothing matches, wherever the clause sits"),
    Case("var r = 'x'; switch ('b') { case 'a': r = 'A'; case 'b': r = 'B'; case 'c': r = 'C'; } String(r)",
         "fall-through: a matched clause runs to the end of the switch"),
    Case("var r = 0; switch ('1') { case 1: r = 10; break; case '1': r = 11; break; } String(r)",
         "the match is a strict equality, not a coercion ('1' is not 1)"),
    Case("var r = 0; switch (1) { case 1: r = 1; } switch (2) { case 1: r = 2; } String(r)",
         "a switch runs when nothing matches and one of no default exits cleanly"),
    Case("var n = 0; for (var i = 0; i < 3; i++) { switch (i) { case 1: continue; default: n += 1; } } String(n)",
         "continue inside a switch reaches the enclosing loop, not the switch"),
    Case("var r = 0; switch (1) { default: r = 5; break; case 1: r = 7; } String(r)",
         "fall-through from a matched clause into a default that sits after it"),
    Case("var r = 0; switch (3) { case 1: r += 1; case 2: r += 2; case 3: r += 3; default: r += 100; } String(r)",
         "fall-through chains from the only match to the end"),
    Case("var d = 0; function side() { d += 1; return 2; } var r = 0; switch (side()) { case 1: r = 1; break; case 2: r = d; } String(r) + '/' + String(d)",
         "the discriminant is evaluated exactly once"),

    # Object-literal spread, which the compiler refused with "This object
    # literal form is not supported by the bytecode compiler". The spread is
    # evaluated at its position, and CopyDataProperties (ECMA-262 §7.3.24) —
    # the tree-walker's own routine — copies the source's own enumerable keys
    # via a CreateDataProperty each. Null and undefined copy nothing.
    Case("var o = {a: 1, ...{b: 2}}; String(o.b)", "the original refusal"),
    Case("var o = {a: 1, ...undefined, ...null, ...7}; String(o.a) + '/' + String(Object.keys(o).length)",
         "spreading null, undefined or a number copies nothing"),
    Case("var o = {...{a: 1, b: 2, c: 3}, b: 9}; String(o.b) + '/' + String(o.a) + '/' + String(o.c)",
         "an own key written after the spread wins"),
    Case("var o = {a: 1, ...{a: 2, b: 2}, c: 3, ...{c: 4}}; String(o.a) + '/' + String(o.c)",
         "a later spread overwrites an earlier one"),
    Case("var o = {...[7, 8]}; String(o[0]) + '/' + String(o[1]) + '/' + String(Object.keys(o).join(','))",
         "an array spreads its indices"),
    Case("var o = {...'abc'}; String(o[0]) + '/' + String(o[1]) + '/' + String(o[2])",
         "a string spreads its positions"),
    Case("var d = ''; function mark(x) { d += x; return {p: x}; } var o = {...mark('a'), ...mark('b')}; String(d) + '/' + String(o.p)",
         "each spread is evaluated at its position"),
    Case("var o = {a: {...{b: {...{c: 5}}}}}; String(o.a.b.c)",
         "spread composes with nesting"),
    Case("var o = {}; var src = {x: 1}; o['s'] = 9; var p = {...src, s: o.s}; String(p.x) + '/' + String(p.s)",
         "spread next to an ordinary key, the key order agrees"),

    # Tagged templates, which the compiler refused with "Unsupported
    # expression in bytecode compiler". The tag runs first, the substitutions
    # after it, and the template object is built fresh on every evaluation —
    # the tree-walker does the same today, and neither path freezes the
    # arrays or caches per call site. `raw` is defined hidden, as
    # eval_tagged_template does.
    Case("function f() { return 1; } String(f`x`)", "the original refusal"),
    Case("var f = function(s) { return s[0] + '/' + s.raw[0]; }; String(f`abc`)",
         "the tag receives the cooked and the raw strings"),
    Case("var f = function(s, x, y) { return s[0] + x + s[1] + y + s[2]; }; String(f`a${40 + 2}b${6 * 7}c`)",
         "substitutions evaluate in order, left to right"),
    Case("var f = function(s) { return s.raw[0] + '/' + s[0]; }; String(f`\\u0041`)",
         "an escape: raw keeps the escape, cooked does not"),
    Case("var d = ''; var f = function(s) { d += 't'; return s[0].length; }; function sub() { d += 's'; return 'y'; } var r = f`q${sub()}w`; String(d) + '/' + String(r)",
         "the tag runs before the substitutions"),
    Case("var f = function(s) { return Object.keys(s).join(','); }; String(f`ab${1}cd`)",
         "the raw property is not enumerable on the strings array"),
    Case("var o = {f: function(s) { return s[0] + s.raw[0]; }}; String(o.f`xy`)",
         "a member-expression tag is bound to its receiver"),
    # Array binding patterns (Phase 421): original refusal first.
    Case("var [a, b] = [1, 2]; String(a)", "the original refusal"),
    Case("var [a,,b]=[1,2,3]; String(a+','+b)", "elisions still pull"),
    Case("var [a=5,b=4,c=9]=[undefined,null,0]; String(a+','+b+','+c)", "defaults only for undefined"),
    Case("var [[a,b],...c]=[[1,2],3,4]; String(a+','+b+','+c)", "nested patterns and rest"),
    Case("var [a,...b]='abc'; String(a+'/'+b.length+'/'+b)", "string iteration and rest"),
    Case("var src={}; var n=0; src['@@iterator']=function(){return {n:40,next:function(){n++;this.n++;return {value:this.n,done:false};},return:function(){n+=10;return {};}}}; var [a,b]=src; String(a+','+b+'/'+n)", "lazy protocol, receiver, and close"),
    Case("var a=42; function f(){var [a]=[1]; return a;} String(f()+'/'+a)", "function var does not overwrite global"),
    Case("function f(a){var [a,b]=[3,4]; return String(a)+','+b;} String(f(8))", "parameter redeclaration"),
    Case("var out=''; {let [a,b]=[1,2];out=String(a)+String(b);} String(out)", "block lexical bindings"),
    Case("var [a=function(){}]=[]; String(a.name)", "inferred default function name"),
    Case("var [a=3,b=a+2]=[]; String(a+','+b)", "defaults observe earlier bindings"),
    Case("var n=0; var src={}; src['@@iterator']=function(){return {next:function(){n++;return {value:n,done:n>3};}}}; var [a,...b]=src; String(a+'/'+b+'/'+n)", "rest drains the iterator"),
    Case("var a=[1,2]; a['@@iterator']=function(){return {next:function(){return {value:9,done:false};}}}; var [x]=a; String(x)", "array iterator override"),
    Case("var a=[1,2]; a[Symbol.iterator]=function(){return {next:function(){return {value:9,done:false};}}}; var [x]=a; String(x)", "symbol iterator override"),
    Case("var a=42; function f(){let [a]=[1]; return a;} String(f()+'/'+a)", "function lexical scope"),
    Case("function f(a){var [b]=[3]; return String(a)+','+b;} String(f(1,2,3))", "extra call arguments do not shift slots"),

    # for-of over a simple var or identifier (Phase 446).
    Case("var s = \"\"; for (var k of [1,2]) { s += k; } String(s)", "the original refusal"),
    Case("var s=\"\"; for(var x of [1,undefined,3]){s+=String(x)+\",\";} String(s)", "undefined remains an element"),
    Case("var s=\"before\"; for(var x of []) {s=\"bad\";} String(s)", "empty iterable"),
    Case("var s=\"\"; for(var x of \"a💡b\") {s+=x+\"/\";} String(s)", "string code points"),
    Case("var n=0;for(var x of [1,2,3,4]) {if(x==2)continue;if(x==4)break;n+=x;}String(n)", "break and continue"),
    Case("var s=\"\";for(var x of [1,2]){for(var y of [3,4]){s+=x*y+\",\";}}String(s)", "nested loops"),
    Case("var a=9; function f(a){for(var a of [1,2]){}return a;} String(f(6)+\"/\"+a)", "a parameter is updated, not the global"),
    Case("var x=0;for(x of [4,5]){}String(x)", "assignment to an existing name"),
    Case("var n=0;var a=[1,2];a[Symbol.iterator]=function(){return {next:function(){n++;return {value:9,done:n>2};}}};for(var x of a){n+=10;break;}String(x+\"/\"+n)", "overridden array iterator"),
    Case("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){n++;return {}}}};for(var x of a){break;}String(n)", "break closes"),
    Case("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){n++;return {value:n,done:n>3}},return:function(){n+=100;return {}}}};for(var x of a){if(x<3)continue;break;}String(n)", "continue does not close"),
    Case("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){return {value:1,done:false}},return:function(){n++;return {}}}};function f(){for(var x of a){return 7;}}String(f()+\"/\"+n)", "return closes"),
    Case("var s=0;for(var x of [1,2,3]){{let y=x*2;if(x==2)continue;s+=y;}}String(s)", "continue unwinds a block local"),
    Case("var s=0;for(var x of [1,2,3]){{let y=x*2;if(x==2)break;s+=y;}}String(s)", "break unwinds a block local"),
    Case("var s=\"\";for(var x of [1,2,3]){switch(x){case 2:continue;default:s+=x;}}String(s)", "continue from switch"),
    Case("var a=[];for(var x of [1,2]) {a.push(x);}String(a)", "body calls array push"),
    Case("var a=[];a.length=3;var r=\"\";for(var x of a){r+=String(x)+\",\"}String(r)", "sparse length yields holes"),
    Case("var a=[1,2];var s=\"\";for(var x of a){s+=x;if(x==1)a.push(3)}String(s)", "array growth during iteration"),
    Case("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){n++;return {done:n>2,value:n}},return:function(){n+=10;return {}}}};for(var x of a){}String(n)", "normal exhaustion does not close"),
    Case("var n=0;var a={};a[Symbol.iterator]=function(){return {next:function(){n++;return {done:false,value:n}},return:function(){n+=10;return {}}}};function f(){for(var x of a){for(var y of a){return x+y;}}}String(f()+\"/\"+n)", "nested return closes inside out"),
    Case("var n=0;for(var x of [1,2]){var [v]=[x];n+=v;}String(n)", "nested destructuring in the body"),

    Case("var a=[]; a.length=2; Object.defineProperty(a,\"1\",{get:function(){return 9;},configurable:true}); var [u,v]=a; String(v)", "a sparse binding reads an accessor beyond stored elements"),
    Case("var a=[];a.length=2;Object.defineProperty(a,\"1\",{get:function(){return 9;},configurable:true});var s=\"\";for(var x of a){s+=String(x)+\",\"}String(s)", "for-of reads sparse accessors"),
    Case("for(var k of []){} String(k)", "empty var loop still declares the name"),
    Case("var k=7;for(var k of []){} String(k)", "empty loop does not overwrite an existing binding"),
]

# The compiler refuses these. Kept here so the count of what is left has a
# number, and so the day one of them starts working shows up as a failure
# rather than as silence.
REFUSES: list[Case] = [
    Case("function f(){for(var x of [1]){try{throw x}catch(e){return e}finally{}}}f()", "return from catch through finalizer and iterator"),
    Case("function f(){for(var x of [1]){try{return x}finally{}}}f()", "return through finalizer and iterator"),
    Case("function f(){try{}finally{return 2}}f()", "return from finalizer"),
    Case("while(true){try{}finally{break}}", "jump from finalizer"),
    Case("try{}finally{let x=2;var f=function(){return x};f()}", "closure inside finalizer"),
    Case("try { throw [1]; } catch ([e]) { e; }", "catch pattern"),
    Case("while(true){try{throw 1}catch(e){break}}", "jump from catch"),
    Case("try{throw 3}catch(e){function f(){return e}f()}", "function in catch would capture its binding"),
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
