// Phase 531, finding 531: ES2025 Iterator constructor, Iterator.from,
// %WrapForValidIteratorPrototype%, %IteratorHelperPrototype%, and the 11
// %IteratorPrototype% helpers/consumers.
TEST(es2025_iterator_from_and_prototype_helpers) {
    using namespace litebird;

    // ------------------------------------------------------------------
    // 1. Constructor, descriptors, helpers, wrapper, VM, and Worker
    // ------------------------------------------------------------------
    {
        TestPage page;
        JsInterpreter& js = page.js;
        js.eval(R"JS(
            var failures = [];
            function check(label, fn) {
                try {
                    if (fn() !== true) failures.push(label + ': returned non-true');
                } catch (e) {
                    failures.push(label + ': unexpected ' + (e && e.name ? e.name + ': ' + e.message : e));
                }
            }
            function throwsKind(Ctor, fn) {
                try {
                    fn();
                    return false;
                } catch (e) {
                    return e instanceof Ctor;
                }
            }

            // Abstract constructor & subclassing (§27.1.1.1)
            check('typeof Iterator', () => typeof Iterator === 'function');
            check('Iterator.length/name', () => Iterator.length === 0 && Iterator.name === 'Iterator');
            check('Iterator() throws', () => throwsKind(TypeError, () => Iterator()));
            check('new Iterator() throws', () => throwsKind(TypeError, () => new Iterator()));
            check('Reflect.construct self throws', () => throwsKind(TypeError, () => Reflect.construct(Iterator, [], Iterator)));
            check('subclass works', () => {
                class Sub extends Iterator {}
                var s = new Sub();
                var r = Reflect.construct(Iterator, [], Sub);
                return s instanceof Iterator && s instanceof Sub &&
                       r instanceof Iterator && Object.getPrototypeOf(r) === Sub.prototype;
            });
            check('Iterator.prototype descriptor', () => {
                var d = Object.getOwnPropertyDescriptor(Iterator, 'prototype');
                return d.value === Iterator.prototype && !d.writable && !d.enumerable && !d.configurable;
            });

            // SetterThatIgnoresPrototypeProperties on constructor & [Symbol.toStringTag]
            check('constructor accessor', () => {
                var d = Object.getOwnPropertyDescriptor(Iterator.prototype, 'constructor');
                var derived = Object.create(Iterator.prototype);
                derived.constructor = 42;
                var own = Object.getOwnPropertyDescriptor(derived, 'constructor');
                var frozen = Object.freeze(Object.create(Iterator.prototype));
                return typeof d.get === 'function' && typeof d.set === 'function' &&
                       d.get.name === 'get constructor' && d.set.name === 'set constructor' &&
                       d.get.length === 0 && d.set.length === 1 &&
                       !d.enumerable && d.configurable && d.get() === Iterator &&
                       throwsKind(TypeError, () => { Iterator.prototype.constructor = 1; }) &&
                       throwsKind(TypeError, () => { frozen.constructor = 1; }) &&
                       own.value === 42 && own.writable && own.enumerable && own.configurable;
            });
            check('toStringTag accessor', () => {
                var d = Object.getOwnPropertyDescriptor(Iterator.prototype, Symbol.toStringTag);
                var derived = Object.create(Iterator.prototype);
                derived[Symbol.toStringTag] = 'Custom';
                var frozen = Object.freeze(Object.create(Iterator.prototype));
                return typeof d.get === 'function' && typeof d.set === 'function' &&
                       d.get.name === 'get [Symbol.toStringTag]' &&
                       d.set.name === 'set [Symbol.toStringTag]' &&
                       d.get.length === 0 && d.set.length === 1 &&
                       !d.enumerable && d.configurable &&
                       Object.prototype.toString.call(Iterator.prototype) === '[object Iterator]' &&
                       throwsKind(TypeError, () => { Iterator.prototype[Symbol.toStringTag] = 'X'; }) &&
                       throwsKind(TypeError, () => { frozen[Symbol.toStringTag] = 'X'; }) &&
                       derived[Symbol.toStringTag] === 'Custom';
            });
            check('@@iterator name & primitive this', () => {
                var d = Object.getOwnPropertyDescriptor(Iterator.prototype, Symbol.iterator);
                return d.value.name === '[Symbol.iterator]' && d.value.length === 0 &&
                       d.value.call(7) === 7;
            });

            // Built-in iterator prototype chains
            check('built-in iterator inheritance & tags', () => {
                return ([].values() instanceof Iterator) &&
                       (new Map([[1, 2]]).entries() instanceof Iterator) &&
                       (new Set([1]).values() instanceof Iterator) &&
                       ('a'[Symbol.iterator]() instanceof Iterator) &&
                       ('a'.matchAll(/a/g) instanceof Iterator) &&
                       Object.prototype.toString.call([].values()) === '[object Array Iterator]' &&
                       Object.prototype.toString.call(new Map().values()) === '[object Map Iterator]' &&
                       Object.prototype.toString.call(new Set().values()) === '[object Set Iterator]' &&
                       Object.prototype.toString.call('a'[Symbol.iterator]()) === '[object String Iterator]' &&
                       Object.prototype.toString.call('a'.matchAll(/a/g)) === '[object RegExp String Iterator]';
            });

            // Iterator.from & %WrapForValidIteratorPrototype%
            check('Iterator.from primitive handling', () => {
                var bad = [undefined, null, true, false, 0, 42, Symbol('s')];
                for (var i = 0; i < bad.length; i++) {
                    if (!throwsKind(TypeError, () => Iterator.from(bad[i]))) return false;
                }
                // Plain non-iterable object is wrapped by GetIteratorFlattenable and throws on .next()
                var emptyObjWrap = Iterator.from({});
                var s = Iterator.from('a\uD83D\uDE00');
                return (emptyObjWrap instanceof Iterator) &&
                       throwsKind(TypeError, () => emptyObjWrap.next()) &&
                       (s instanceof Iterator) &&
                       s.next().value === 'a' &&
                       s.next().value === '\uD83D\uDE00' &&
                       s.next().done === true;
            });
            check('Iterator.from string throws if String@@iterator removed', () => {
                var orig = Object.getOwnPropertyDescriptor(String.prototype, Symbol.iterator);
                try {
                    Object.defineProperty(String.prototype, Symbol.iterator, { value: undefined, configurable: true });
                    return throwsKind(TypeError, () => Iterator.from('abc'));
                } finally {
                    Object.defineProperty(String.prototype, Symbol.iterator, orig);
                }
            });
            check('Iterator.from wrapper semantics & return forwarding', () => {
                var log = [];
                var base = {
                    get next() {
                        log.push('get-next');
                        return function () { log.push('call-next'); return { value: 7, done: false }; };
                    },
                    get return() {
                        log.push('get-ret');
                        return function () { log.push('call-ret'); return 42; };
                    }
                };
                var w = Iterator.from(base);
                var existing = [1, 2].values();
                var noRet = Iterator.from({ next() { return { done: false }; } });
                var badRet = Iterator.from({ next() { return { done: false }; }, return: 9 });
                return w instanceof Iterator && w !== base &&
                       Iterator.from(existing) === existing &&
                       Iterator.from(w) === w &&
                       Object.prototype.toString.call(w) === '[object Iterator]' &&
                       w.next().value === 7 && w.next().value === 7 &&
                       w.return() === 42 &&
                       noRet.return().done === true &&
                       throwsKind(TypeError, () => badRet.return()) &&
                       log.join(',') === 'get-next,call-next,call-next,get-ret,call-ret';
            });

            // Lazy helpers: map, filter, take, drop, flatMap + argument validation closes underlying
            check('validation failure closes underlying before get next', () => {
                var closed = 0;
                var mk = () => ({
                    __proto__: Iterator.prototype,
                    get next() { failures.push('next read during invalid arg'); return () => ({ done: true }); },
                    return() { closed++; return {}; }
                });
                var ok = throwsKind(TypeError, () => mk().map(123)) &&
                         throwsKind(TypeError, () => mk().filter()) &&
                         throwsKind(TypeError, () => mk().flatMap(null)) &&
                         throwsKind(TypeError, () => mk().forEach(1)) &&
                         throwsKind(TypeError, () => mk().some(1)) &&
                         throwsKind(TypeError, () => mk().every(1)) &&
                         throwsKind(TypeError, () => mk().find(1)) &&
                         throwsKind(TypeError, () => mk().reduce(1)) &&
                         throwsKind(RangeError, () => mk().take(NaN)) &&
                         throwsKind(RangeError, () => mk().take(-1)) &&
                         throwsKind(RangeError, () => mk().take()) &&
                         throwsKind(RangeError, () => mk().drop(NaN)) &&
                         throwsKind(RangeError, () => mk().drop(-Infinity)) &&
                         throwsKind(RangeError, () => mk().drop(undefined));
                return ok && closed === 14;
            });

            check('take/drop limits including ES2025 Number.MAX_SAFE_INTEGER + 1', () => {
                var log = [];
                var src = {
                    __proto__: Iterator.prototype,
                    i: 0,
                    next() { log.push('n' + this.i); return { value: this.i++, done: false }; },
                    return() { log.push('ret'); return {}; }
                };
                var t = src.take(2);
                var r0 = t.next().value;
                var r1 = t.next().value;
                var r2 = t.next();
                var r3 = t.next();

                var closedZero = 0;
                var z = Iterator.from({ next() { return { done: true }; }, return() { closedZero++; return {}; } }).take(0);
                var zres = z.next();

                var dropSrc = {
                    __proto__: Iterator.prototype,
                    i: 0,
                    next() { return this.i < 3 ? { value: this.i++, done: false } : { done: true }; },
                    return() { throw new Error('drop must not call return on exhaustion'); }
                };
                return r0 === 0 && r1 === 1 && r2.done === true && r3.done === true &&
                       log.join(',') === 'n0,n1,ret' &&
                       closedZero === 1 && zres.done === true &&
                       dropSrc.drop(1.9).toArray().join(',') === '1,2' &&
                       [1, 2, 3].values().take(Number.MAX_SAFE_INTEGER + 1).toArray().join(',') === '1,2,3' &&
                       [1, 2, 3].values().drop(Number.MAX_SAFE_INTEGER + 1).toArray().length === 0 &&
                       [1, 2, 3].values().take(-0.5).toArray().length === 0 &&
                       [1, 2, 3].values().drop(-0.5).toArray().join(',') === '1,2,3';
            });

            check('map/filter/flatMap/consumers & reentrancy', () => {
                var chained = [1, 2, 3, 4, 5].values()
                    .filter((x, i) => (x + i) % 2 === 1)
                    .map(x => [x, x * 10])
                    .flatMap(pair => pair)
                    .drop(1)
                    .take(4)
                    .toArray();

                // flatMap closes inner then outer on return, and only outer when inner.next throws
                var fmLog1 = [];
                var outer1 = {
                    __proto__: Iterator.prototype,
                    next() { fmLog1.push('on'); return { value: 1, done: false }; },
                    return() { fmLog1.push('or'); return {}; }
                };
                var inner1 = {
                    next() { fmLog1.push('in'); return { value: 9, done: false }; },
                    return() { fmLog1.push('ir'); return {}; }
                };
                var fm1 = outer1.flatMap(() => inner1);
                var stepVal = fm1.next().value;
                fm1.return();

                var fmLog2 = [];
                var outer2 = {
                    __proto__: Iterator.prototype,
                    next() { fmLog2.push('on'); return { value: 1, done: false }; },
                    return() { fmLog2.push('or'); return {}; }
                };
                var inner2 = {
                    next() { fmLog2.push('in'); throw new URIError('inner-boom'); },
                    return() { fmLog2.push('ir'); return {}; }
                };
                var fm2 = outer2.flatMap(() => inner2);
                var threwUri = throwsKind(URIError, () => fm2.next());

                // Caught reentrancy leaves outer call executing normally
                var h;
                var reentrantErrs = 0;
                h = [3, 4].values().map(v => {
                    try { h.next(); } catch (e) { if (e instanceof TypeError) reentrantErrs++; }
                    try { h.return(); } catch (e) { if (e instanceof TypeError) reentrantErrs++; }
                    return v + 1;
                });
                var outVals = h.toArray();

                // Return on SuspendedStart closes underlying
                var suspClosed = 0;
                var susp = Iterator.from({ next() { return { value: 1, done: false }; }, return() { suspClosed++; return {}; } })
                    .map(x => x);
                susp.return();

                // Consumers & short-circuit closing & reduce with explicit undefined initialValue
                var closedBySome = 0, closedByEvery = 0, closedByFind = 0;
                var mkStream = (onClose) => ({
                    __proto__: Iterator.prototype,
                    i: 0,
                    next() { return this.i < 5 ? { value: this.i++, done: false } : { done: true }; },
                    return() { onClose(); return {}; }
                });
                var sRes = mkStream(() => { closedBySome++; }).some(v => v === 2);
                var eRes = mkStream(() => { closedByEvery++; }).every(v => v < 2);
                var fRes = mkStream(() => { closedByFind++; }).find(v => v === 3);
                var fe = [];
                ['a', 'b'].values().forEach((v, idx) => fe.push(v + idx));
                var r1 = [10, 20, 30].values().reduce((a, v, idx) => a + v + idx);
                var r2 = [10, 20].values().reduce((a, v, idx) => String(a) + ':' + v + ':' + idx, undefined);
                var rEmptyErr = throwsKind(TypeError, () => [].values().reduce((a, b) => a + b));

                return chained.join(',') === '10,2,20,3' &&
                       Object.prototype.toString.call(h) === '[object Iterator Helper]' &&
                       stepVal === 9 && fmLog1.join(',') === 'on,in,ir,or' &&
                       threwUri && fmLog2.join(',') === 'on,in,or' && fm2.next().done === true &&
                       reentrantErrs === 4 && outVals.join(',') === '4,5' &&
                       suspClosed === 1 &&
                       sRes === true && closedBySome === 1 &&
                       eRes === false && closedByEvery === 1 &&
                       fRes === 3 && closedByFind === 1 &&
                       fe.join(',') === 'a0,b1' &&
                       r1 === 63 && r2 === 'undefined:10:0:20:1' && rEmptyErr;
            });

            check('IteratorStepValue safety & Object.prototype setter immunity & structuredClone', () => {
                var closed = 0;
                var badNext = Iterator.from({
                    next() { return 42; },
                    return() { closed++; return {}; }
                });
                var valueRead = false;
                var doneTrue = Iterator.from({
                    next() { return { done: true, get value() { valueRead = true; return 1; } }; }
                });
                var setterHit = false;
                var stepObj, rawStepObj;
                try {
                    // Use null-prototype descriptors so installing 'value' on Object.prototype
                    // does not cause the subsequent 'done' descriptor to inherit a 'value' property.
                    Object.defineProperty(Object.prototype, 'value', {
                        __proto__: null,
                        set() { setterHit = true; },
                        configurable: true
                    });
                    Object.defineProperty(Object.prototype, 'done', {
                        __proto__: null,
                        set() { setterHit = true; },
                        configurable: true
                    });
                    rawStepObj = [9].values().next();
                    stepObj = [9].values().map(x => x).next();
                } finally {
                    delete Object.prototype.value;
                    delete Object.prototype.done;
                }
                var cloneErr1 = false, cloneErr2 = false;
                try { structuredClone(Iterator.from([])); } catch (e) { cloneErr1 = e && e.name === 'DataCloneError'; }
                try { structuredClone([1].values().map(x => x)); } catch (e) { cloneErr2 = e && e.name === 'DataCloneError'; }
                return throwsKind(TypeError, () => badNext.map(x => x).next()) &&
                       closed === 0 &&
                       doneTrue.toArray().length === 0 && !valueRead &&
                       !setterHit &&
                       rawStepObj.value === 9 && rawStepObj.done === false &&
                       stepObj.value === 9 && stepObj.done === false &&
                       cloneErr1 && cloneErr2;
            });

            check('for..of, spread, destructuring on helpers & wrappers', () => {
                var h = [1, 2, 3, 4].values().filter(x => x % 2 === 0).map(x => x * 10);
                var spread = [...h];
                var acc = 0;
                for (var v of [1, 2, 3].values().map(x => x + 1)) acc += v;
                var pair = Iterator.from({ i: 5, next() { return { value: this.i++, done: false }; } });
                var [a, b] = pair;
                return spread.join(',') === '20,40' && acc === 9 && a === 5 && b === 6;
            });
        )JS");
        const std::string failed = js.eval("failures.join('\\n')").to_string();
        if (!failed.empty()) std::cerr << "Iterator suite failures:\n" << failed << "\n";
        assert(failed.empty());

        // Bytecode VM check
        const JsValue vm_res = js.eval_bytecode(
            "var sum = 0;"
            "for (var x of [1, 2, 3, 4].values().filter(n => n > 1).map(n => n * 3)) {"
            "  sum = sum + x;"
            "}"
            "sum + ':' + [10, 20].values().take(1).toArray().join(',');");
        assert(vm_res.to_string() == "27:10");

        // Worker realm check
        js.eval(
            "var workerDone = null;"
            "var w = new Worker('data:text/javascript,' + encodeURIComponent("
            "  'postMessage(typeof Iterator + \":\" + [1,2,3].values().map(x=>x+10).take(2).toArray().join(\",\"));'"
            "));"
            "w.onmessage = function(e) { workerDone = e.data; };"
            "w.onerror = function(e) { workerDone = 'ERR:' + e.message; };");
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < std::chrono::seconds(5)) {
            EventLoop::default_loop().tick();
            EventLoop::default_loop().run_microtasks();
            if (js.eval("workerDone !== null").is_truthy()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(js.eval("workerDone").to_string() == "function:11,12");
        js.eval("w.terminate();");
        WorkerObject::shutdown_all_workers();
    }

    // ------------------------------------------------------------------
    // 2. GC cycle reclamation over %WrapForValidIteratorPrototype% & helpers
    // ------------------------------------------------------------------
    // Note: built-in array iterators (`[1,2,3].values()`) use the pre-existing
    // `js_make_iterator` helper whose std::function captures a duplicate handle
    // alongside `roots`; use `Iterator.from` here so this test isolates cycles
    // through `%WrapForValidIteratorPrototype%` and `%IteratorHelperPrototype%`.
    auto alive_now = []() {
        return Heap::default_heap().collect_garbage({}).alive_cells;
    };
    // Run a settling pass so multi-hop DOM/Worker cleanup from earlier work finishes before baseline.
    alive_now();
    const size_t baseline = alive_now();
    {
        JsInterpreter js;
        js.eval(R"JS(
            for (var i = 0; i < 2000; i++) {
                var target = {
                    next() { return { value: target, done: false }; },
                    return() { return { value: wrap, done: true }; }
                };
                var wrap = Iterator.from(target);
                target.wrap = wrap;
                wrap.target = target;

                var h = wrap
                    .map(function (x) { return { x: x, h: h, wrap: wrap }; })
                    .filter(function () { return true; })
                    .take(2)
                    .drop(1)
                    .flatMap(function () {
                        var inner = { next() { return { value: h, done: false }; }, return() { return {}; } };
                        inner.owner = h;
                        return inner;
                    });
                h.self = h;
                if (i % 2 === 0) {
                    h.next();
                    if (i % 4 === 0) h.return();
                }
            }
        )JS");
    }
    const size_t after = alive_now();
    if (after != baseline) {
        std::cerr << "Iterator cycle delta: baseline=" << baseline << " after=" << after << "\n";
    }
    assert(after == baseline);
}
