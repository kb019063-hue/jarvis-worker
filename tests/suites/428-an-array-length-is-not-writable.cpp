// Phase 428, finding 428: an array's non-writable length was written through.
//
// `Object.defineProperty(a, "length", { writable: false })` marks `a.length`
// read-only, and `Object.getOwnPropertyDescriptor` reported that bit all along.
// Every path that moved the length ignored it:
//
//     var a = [];
//     Object.defineProperty(a, "length", { writable: false });
//     Object.defineProperty(a, "length", { value: 12 });   // a.length became 12
//     Object.defineProperty(a, "0", { value: 1 });         // and then 1
//     a[0] = 1;                                            // and then 1
//
// where Node v22.22.3 throws a TypeError for the first two and refuses the
// third. The same bit is what `Object.freeze` gives an array's length, and what
// `Object.seal` does *not*: sealing leaves the length writable, so a sealed
// array may still grow but may not shrink, because shrinking deletes places and
// a sealed array's places are not configurable. That distinction is half the
// cases here.
//
// `ArraySetLength` (§10.4.2.4) begins "let oldLenDesc be OrdinaryGetOwnProperty
// (A, 'length')" and the engine began from the element count in both of its
// copies of the algorithm — the descriptor path in `BuiltinsObject.cpp` and the
// `[[Set]]` path in `JsValue.cpp` — which is why one bit was wrong in six
// places at once. Every answer below is Node v22.22.3's, checked by running the
// two engines side by side, and test262 agrees: `defineProperty` and
// `defineProperties` between them went from 14 of these failures to none.
//
// The cases are collected rather than thrown. An exception that leaves a suite
// is printed and the run still reports the suite PASSED, so a suite that threw
// would pass whatever it found; `fail()` records, and the C++ below asserts the
// record is empty.
TEST(an_array_length_is_not_writable) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function fail(what) { failures.push(what); }
        function same(got, want, what) {
            if (got !== want) fail(what + ': ' + got + ' !== ' + want);
        }
        function threw(f, Kind, what) {
            try { f(); } catch (e) {
                if (!(e instanceof Kind)) { fail(what + ': threw ' + e.name); return; }
                return;
            }
            fail(what + ': nothing was thrown');
        }

        // A read-only length takes no new value, and asking again for the one
        // it has is not a new value (§10.4.2.4 step 3: SameValue answers true).
        (function () {
            var a = [];
            Object.defineProperty(a, 'length', { writable: false });
            threw(function () { Object.defineProperty(a, 'length', { value: 12 }); },
                  TypeError, 'a read-only length took a new value');
            same(a.length, 0, 'length after the refusal');
            Object.defineProperty(a, 'length', { value: 0 });   // the value it has: allowed
            same(a.length, 0, 'length after re-defining the same value');
        })();

        // A new place past it: through a descriptor, through a sloppy write
        // (refused silently), and through a strict one (which throws).
        (function () {
            var a = [];
            Object.defineProperty(a, 'length', { writable: false });
            threw(function () { Object.defineProperty(a, '0', { value: 1 }); },
                  TypeError, 'defineProperty grew a read-only length');
            same(a.length, 0, 'length after defineProperty of an index');
            same(0 in a, false, 'the place defineProperty refused');

            var b = [];
            Object.defineProperty(b, 'length', { writable: false });
            b[0] = 1;
            same(b.length, 0, 'a sloppy write grew it');
            same(0 in b, false, 'a sloppy write landed');

            var c = [];
            Object.defineProperty(c, 'length', { writable: false });
            threw(function () { (function () { 'use strict'; c[0] = 1; })(); },
                  TypeError, 'a strict write past a read-only length');
            same(c.length, 0, 'length after the strict write');
        })();

        // Freeze: the length is read-only, so it neither grows nor shrinks.
        (function () {
            var a = Object.freeze([1]);
            threw(function () { Object.defineProperty(a, 'length', { value: 3 }); },
                  TypeError, 'a frozen array grew');
            same(a.length, 1, 'the length a frozen array kept');
            var d = Object.getOwnPropertyDescriptor(a, 'length');
            same(d.writable + ',' + d.configurable + ',' + d.enumerable,
                 'false,false,false', 'a frozen length descriptor');
        })();

        // Seal: the length stays writable, so a grow is allowed; a shrink is
        // not, because the places above the new end cannot be deleted.
        (function () {
            var a = Object.seal([1]);
            a.length = 5;
            same(a.length, 5, 'a sealed array grew');
            a[0] = 5;
            same(a[0], 5, 'a sealed place took a value');

            var b = Object.seal([1, 2, 3]);
            threw(function () { Object.defineProperty(b, 'length', { value: 1 }); },
                  TypeError, 'a sealed array shrank');
            same(b.length, 3, 'the length a sealed array kept');

            var c = Object.seal([1, 2, 3]);
            threw(function () { (function () { 'use strict'; c.length = 1; })(); },
                  TypeError, 'a strict assignment shrank a sealed array');
            same(c.length, 3, 'the length after the refused assignment');
        })();

        // Nothing ordinary changed: an array still grows and shrinks, a place
        // that cannot be deleted still stops a shrink where it stands, and an
        // invalid length is still a RangeError.
        (function () {
            var a = [1, 2, 3];
            a.length = 5;
            same(a.length, 5, 'a grow');
            a.length = 1;
            same(a.length + ',' + a[0], '1,1', 'a shrink');

            var b = [1, 2, 3];
            Object.defineProperty(b, '2', { configurable: false });
            threw(function () { Object.defineProperty(b, 'length', { value: 1 }); },
                  TypeError, 'a shrink past a non-configurable place');
            same(b.length, 3, 'the length a refused shrink leaves');

            var c = [];
            threw(function () { c.length = -1; }, RangeError, 'an invalid length');
        })();

        // The order of the two refusals, which §10.4.2.4 asks for: a value that
        // is not a length is a RangeError (step 3.a) before the `[[Writable]]`
        // check (step 3.b), so `defineProperty` on a read-only length reports
        // the RangeError; but `[[Set]]` asks the descriptor and gives up at the
        // writable bit, so a strict assignment to the same property is a
        // TypeError. Node v22.22.3 answers both that way.
        (function () {
            var a = [];
            Object.defineProperty(a, 'length', { writable: false });
            threw(function () { Object.defineProperty(a, 'length', { value: -1 }); },
                  RangeError, 'defineProperty of an invalid length');
            threw(function () { Object.defineProperty(a, 'length', { value: 1.5 }); },
                  RangeError, 'defineProperty of a fractional length');
            threw(function () { (function () { 'use strict'; a.length = -1; })(); },
                  TypeError, 'a strict assignment of an invalid length');
            same(a.length, 0, 'the length both refusals left');
        })();

        // And the two paths disagree about the value the length already has,
        // which is the SameValue test of step 3 not being reached through
        // `[[Set]]` at all: OrdinarySet gives up at the writable bit, so a
        // strict assignment of the current length throws, while the same thing
        // asked as a descriptor is allowed.
        (function () {
            var a = [1, 2];
            Object.defineProperty(a, 'length', { writable: false });
            threw(function () { (function () { 'use strict'; a.length = a.length; })(); },
                  TypeError, 'a strict assignment of the length it has');
            Object.defineProperty(a, 'length', { value: a.length });
            same(a.length, 2, 'the length after re-defining the value it has');

            var b = Object.freeze([1, 2]);
            threw(function () { (function () { 'use strict'; b.length = 2; })(); },
                  TypeError, 'a strict assignment of a frozen length');
            Object.defineProperty(b, 'length', { value: 2 });
            same(b.length, 2, 'a frozen length re-defined with its own value');
        })();

        // `push` onto a read-only length is a TypeError, not a silent no-op.
        (function () {
            var a = [];
            Object.defineProperty(a, 'length', { writable: false });
            threw(function () { a.push(1); }, TypeError, 'push onto a read-only length');
            same(a.length, 0, 'length after push');
        })();

        // The result-building methods still work on an ordinary array, which is
        // what a fix that refused every length write would break.
        (function () {
            var a = [];
            a.push(1, 2, 3);
            same(a.length + ',' + a.join(''), '3,123', 'push on an ordinary array');
            a.pop();
            same(a.length, 2, 'pop');
            var b = [1, 2, 3];
            b.length = 0;
            same(b.length + ',' + b.join(''), '0,', 'a plain shrink to nothing');
        })();
    )JS");

    // An exception that leaves the script is printed by the interpreter and the
    // suite still reports PASSED, so a suite that threw would pass whatever it
    // found. What the cases recorded is the result, and it must be empty.
    const std::string failed = js.eval("failures.join('\\n')").to_string();
    if (!failed.empty()) std::cerr << failed << "\n";
    assert(failed.empty());
}
