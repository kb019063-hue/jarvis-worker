// Phase 252, finding 256: structuredClone keeps the type of what it copies.
//
// HTML §2.7.6 (StructuredSerializeInternal) does not flatten a Map into an
// object of its own properties; it clones the entry list, and the same for a
// Set, a RegExp's pattern and flags, and the bytes of an ArrayBuffer, a typed
// array and a DataView. This engine's structuredClone walked own enumerable
// properties for everything it was given, so `structuredClone(new Map([["a",
// 1]]))` was `{}`, `structuredClone(/ab+c/gi)` was `{}`, and a typed array came
// back a plain object; a transfer list was ignored outright. Node v22.22.3 is
// the oracle for every answer below.
TEST(structured_clone_keeps_the_type_of_what_it_copies) {
    TestPage page;
    JsInterpreter& js = page.js;
    js.eval(R"JS(
        var failures = [];
        function ok(what, got, want) {
            if (String(got) !== String(want)) failures.push(what + ': got ' + got + ', expected ' + want);
        }
        function okThrows(what, fn, name) {
            try { fn(); failures.push(what + ': no throw, expected ' + name); }
            catch (e) { if (e.name !== name) failures.push(what + ': threw ' + e.name + ', expected ' + name); }
        }

        // ---- Map and Set keep their entries, in order ----------------------
        var key = {};
        var map = new Map([[key, { v: 1 }], ['b', 2]]);
        var cloned = structuredClone(map);
        ok('Map is a Map', cloned instanceof Map, true);
        ok('Map size', cloned.size, 2);
        ok('Map order', Array.from(cloned.keys()).map(function (k) { return typeof k === 'string' ? k : 'object'; }).join(','), 'object,b');
        var cloned_key = Array.from(cloned.keys())[0];
        ok('a key is cloned, not shared', cloned_key === key, false);
        ok('its value comes with it', cloned.get(cloned_key).v, 1);
        ok('the old key is not a key of the clone', cloned.has(key), false);

        var set = structuredClone(new Set([3, 1, 2]));
        ok('Set is a Set', set instanceof Set, true);
        ok('Set order', Array.from(set.values()).join(','), '3,1,2');

        // A collection that holds itself is a collection that holds itself.
        var self_map = new Map();
        self_map.set('me', self_map);
        var self_clone = structuredClone(self_map);
        ok('a Map that holds itself', self_clone.get('me') === self_clone, true);

        // Two references to one object are one object, inside a collection too.
        var shared = { s: 1 };
        var pair = structuredClone(new Set([shared, shared]));
        ok('a shared value is one value', pair.size, 1);

        // ---- RegExp keeps its pattern, its flags and a fresh lastIndex ------
        var re = structuredClone(new RegExp('a+b', 'gi'));
        ok('RegExp is a RegExp', re instanceof RegExp, true);
        ok('source', re.source, 'a+b');
        ok('flags', re.flags, 'gi');
        var sticky = /x/g;
        sticky.lastIndex = 5;
        ok('lastIndex starts at 0', structuredClone(sticky).lastIndex, 0);
        ok('the pattern still runs', JSON.stringify(structuredClone(/b(c)/).exec('abc')), '["bc","c"]');

        // ---- the byte types keep their bytes and their type -----------------
        var u8 = structuredClone(new Uint8Array([7, 8]));
        ok('Uint8Array is a Uint8Array', u8 instanceof Uint8Array, true);
        ok('its bytes', u8.join('/'), '7/8');
        var u16 = structuredClone(new Uint16Array([258, 515]));
        ok('Uint16Array keeps its width', u16.join('/') + '/' + u16.buffer.byteLength, '258/515/4');
        var f64 = structuredClone(new Float64Array([1.5, 2.5]));
        ok('Float64Array is a Float64Array', f64 instanceof Float64Array, true);
        var buf = structuredClone(new ArrayBuffer(4));
        ok('ArrayBuffer is an ArrayBuffer', buf instanceof ArrayBuffer, true);
        ok('its length', buf.byteLength, 4);
        var dv = new DataView(new ArrayBuffer(4));
        dv.setUint8(0, 7);
        var dv_clone = structuredClone(dv);
        ok('DataView is a DataView', dv_clone instanceof DataView, true);
        ok('its bytes', dv_clone.getUint8(0) + '/' + dv_clone.byteLength, '7/4');

        // A view and the buffer it is a window on clone to one buffer: the
        // graph keeps its shape, which is what the seen map is for.
        var buffer = new ArrayBuffer(8);
        var view = new Uint8Array(buffer);
        view[0] = 9;
        var graph = structuredClone({ b: buffer, v: view });
        ok('the buffer clone is a buffer', graph.b instanceof ArrayBuffer, true);
        ok('the view shares the cloned buffer', graph.v.buffer === graph.b, true);
        ok('and its bytes', graph.v[0], 9);

        // ---- the transfer list moves, it does not copy ----------------------
        var movable = new ArrayBuffer(4);
        new Uint8Array(movable)[0] = 9;
        var moved = structuredClone(movable, { transfer: [movable] });
        ok('the clone has the bytes', moved.byteLength + '/' + new Uint8Array(moved)[0], '4/9');
        ok('the source is detached', movable.byteLength, 0);
        okThrows('a view is not a transferable', function () {
            structuredClone(new Uint8Array(new ArrayBuffer(4)), { transfer: [new Uint8Array(new ArrayBuffer(4))] });
        }, 'DataCloneError');
        okThrows('one buffer listed twice', function () {
            var twice = new ArrayBuffer(4);
            structuredClone({}, { transfer: [twice, twice] });
        }, 'DataCloneError');

        // ---- what the standard refuses is refused ---------------------------
        okThrows('a function', function () { structuredClone(function () {}); }, 'DataCloneError');
        okThrows('a WeakMap', function () { structuredClone(new WeakMap()); }, 'DataCloneError');
        okThrows('a Promise', function () { structuredClone(Promise.resolve(1)); }, 'DataCloneError');
        // A Symbol has no clone at all. This assertion used to say `TypeError`
        // and a comment here said that was "the engine's own wording rather
        // than Node's DataCloneError". It was not a choice: HTML 2.7.4
        // StructuredSerializeInternal step 6 is "If value is a Symbol, then
        // throw a DataCloneError DOMException", in its own step before any
        // object is looked at, and Node answers exactly that — `e.name` is
        // DataCloneError, `e.code` 25. Corrected in Phase 413.
        okThrows('a Symbol', function () { structuredClone(Symbol('x')); }, 'DataCloneError');

        // ---- what stays as it was -------------------------------------------
        var date = structuredClone(new Date(86400000));
        ok('Date still carries its time value', date.toISOString(), '1970-01-02T00:00:00.000Z');
        var err = structuredClone(new Error('boom', { cause: 42 }));
        ok('Error keeps message, cause and type', err.message + '/' + err.cause + '/' + (err instanceof Error), 'boom/42/true');
        var cyclic = { a: 1 };
        cyclic.self = cyclic;
        var cyclic_clone = structuredClone(cyclic);
        ok('a cycle is a cycle', cyclic_clone.self === cyclic_clone, true);
        var sibling = { s: 1 };
        var two = structuredClone({ x: sibling, y: sibling });
        ok('a shared object is one object', two.x === two.y, true);
        var plain = structuredClone({ n: 1, a: [1, 2] });
        ok('an ordinary object is unchanged', JSON.stringify(plain), '{"n":1,"a":[1,2]}');

        failures.length ? failures.join('\n') : 'ok';
    )JS");
    const std::string failed = js.eval("failures.length ? failures.join('\\n') : ''").to_string();
    if (!failed.empty()) std::cerr << failed << "\n";
    assert(failed.empty());
}
