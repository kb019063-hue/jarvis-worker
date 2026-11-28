// Phase 505, finding 505: ES2025 Set.prototype set methods (ECMA-262 §24.2.3).
TEST(set_prototype_es2025_set_methods) {
    TestPage page;
    page.js.eval("globalThis").define_property("forceGc", js_make_native("forceGc", 0, [](const std::vector<JsValue>&) {
        Heap::default_heap().collect_garbage();
        return JsValue::make_undefined();
    }), PropertyMap::Writable | PropertyMap::Configurable);

    JsValue result = page.js.eval(R"JS(
        (function () {
            var failures = [];
            function check(name, cond) {
                if (!cond) failures.push(name);
            }
            function arr(set) {
                return Array.from(set);
            }
            function eqArr(a, b) {
                if (a.length !== b.length) return false;
                for (var i = 0; i < a.length; i++) {
                    if (!Object.is(a[i], b[i])) return false;
                }
                return true;
            }
            function throwsKind(kind, fn) {
                try {
                    fn();
                    return false;
                } catch (e) {
                    return e instanceof kind;
                }
            }

            // 1. Metadata & non-constructible descriptors.
            var methods = [
                "union",
                "intersection",
                "difference",
                "symmetricDifference",
                "isSubsetOf",
                "isSupersetOf",
                "isDisjointFrom"
            ];
            for (var i = 0; i < methods.length; i++) {
                var m = methods[i];
                var fn = Set.prototype[m];
                var desc = Object.getOwnPropertyDescriptor(Set.prototype, m);
                check(m + ":function", typeof fn === "function");
                check(m + ":name", fn.name === m);
                check(m + ":length", fn.length === 1);
                check(m + ":desc", !!desc && desc.writable && !desc.enumerable && desc.configurable);
                check(m + ":not-ctor", throwsKind(TypeError, function () { new fn(); }));
            }

            // 2. Core algebra, NaN, -0 -> +0 canonicalization, objects, Map & self.
            var o1 = { id: 1 }, o2 = { id: 2 }, o3 = { id: 3 };
            var s1 = new Set([1, 2, NaN, o1, o2, -0]);
            var s2 = new Set([2, 3, NaN, o2, o3, +0]);
            check("union:values", eqArr(arr(s1.union(s2)), [1, 2, NaN, o1, o2, +0, 3, o3]));
            check("intersection:values", eqArr(arr(s1.intersection(s2)), [2, NaN, o2, +0]));
            check("difference:values", eqArr(arr(s1.difference(s2)), [1, o1]));
            check("symDiff:values", eqArr(arr(s1.symmetricDifference(s2)), [1, o1, 3, o3]));
            check("isSubsetOf:sub", new Set([NaN, -0, o1]).isSubsetOf(s1));
            check("isSubsetOf:not-sub", !s1.isSubsetOf(s2));
            check("isSupersetOf:super", s1.isSupersetOf(new Set([NaN, +0, o1])));
            check("isSupersetOf:not-super", !s1.isSupersetOf(s2));
            check("isDisjointFrom:false", !s1.isDisjointFrom(s2));
            check("isDisjointFrom:true", new Set([1, o1]).isDisjointFrom(new Set([2, o2])));

            // Empty sets and self-arguments.
            var empty = new Set();
            check("empty:union", eqArr(arr(empty.union(empty)), []));
            check("empty:isSubsetOf", empty.isSubsetOf(empty));
            check("empty:isSupersetOf", empty.isSupersetOf(empty));
            check("empty:isDisjointFrom", empty.isDisjointFrom(empty));
            check("self:union", eqArr(arr(s1.union(s1)), arr(s1)) && s1.union(s1) !== s1);
            check("self:intersection", eqArr(arr(s1.intersection(s1)), arr(s1)) && s1.intersection(s1) !== s1);
            check("self:difference", eqArr(arr(s1.difference(s1)), []));
            check("self:symDiff", eqArr(arr(s1.symmetricDifference(s1)), []));
            check("self:predicates", s1.isSubsetOf(s1) && s1.isSupersetOf(s1) && !s1.isDisjointFrom(s1));

            // Map and array-with-set-like-properties as argument.
            var mp = new Map([[2, "two"], [9, "nine"]]);
            check("map:union", eqArr(arr(new Set([1, 2]).union(mp)), [1, 2, 9]));
            check("map:intersection", eqArr(arr(new Set([1, 2, 3]).intersection(mp)), [2]));
            var arrLike = [100];
            arrLike.size = 2;
            arrLike.has = function (v) { return v === 2 || v === 10; };
            arrLike.keys = function () { return [2, 10].values(); };
            check("arrLike:union", eqArr(arr(new Set([1, 2]).union(arrLike)), [1, 2, 10]));

            // 3. Receiver internal slot checked before GetSetRecord(other).
            for (var j = 0; j < methods.length; j++) {
                var methodName = methods[j];
                var badReceivers = [null, undefined, 42, "str", false, {}, [], new Map(), new WeakSet()];
                for (var k = 0; k < badReceivers.length; k++) {
                    var touched = false;
                    var probe = {
                        get size() { touched = true; return 0; },
                        has: function () { return false; },
                        keys: function () { return [].values(); }
                    };
                    check(methodName + ":recv:" + k,
                          throwsKind(TypeError, (function (mn, r) {
                              return function () { Set.prototype[mn].call(r, probe); };
                          })(methodName, badReceivers[k])) && !touched);
                }
            }

            // 4. GetSetRecord step ordering, ToNumber / ToIntegerOrInfinity / errors.
            check("other:primitive", throwsKind(TypeError, function () { s1.union(123); }));
            check("other:undefined", throwsKind(TypeError, function () { s1.union(undefined); }));
            check("other:size-NaN", throwsKind(TypeError, function () {
                s1.union({ size: NaN, has: function () {}, keys: function () { return [].values(); } });
            }));
            check("other:size-undef", throwsKind(TypeError, function () {
                s1.union({ size: undefined, has: function () {}, keys: function () { return [].values(); } });
            }));
            check("other:size-neg", throwsKind(RangeError, function () {
                s1.union({ size: -1, has: function () {}, keys: function () { return [].values(); } });
            }));
            check("other:size-neg-inf", throwsKind(RangeError, function () {
                s1.union({ size: -Infinity, has: function () {}, keys: function () { return [].values(); } });
            }));
            check("other:size-neg-frac-ok", eqArr(arr(new Set([1]).union({
                size: -0.5,
                has: function () { return false; },
                keys: function () { return [2].values(); }
            })), [1, 2]));
            check("other:size-pos-frac-truncates-to-0", new Set().isSupersetOf({
                size: 0.5,
                has: function () { return true; },
                keys: function () { return [1].values(); }
            }) === false);
            check("other:size-pos-inf", new Set([1]).isSubsetOf({
                size: Infinity,
                has: function (v) { return v === 1; },
                keys: function () { throw new Error("should not call keys"); }
            }));
            check("other:has-not-fn", throwsKind(TypeError, function () {
                s1.union({ size: 1, has: 9, keys: function () { return [].values(); } });
            }));
            check("other:keys-not-fn", throwsKind(TypeError, function () {
                s1.isSubsetOf({ size: 1, has: function () { return true; }, keys: {} });
            }));

            // Access order & size-based branch selection.
            var order = [];
            var sl = {
                get size() {
                    order.push("size");
                    return { valueOf: function () { order.push("ToNumber"); return 3; } };
                },
                get has() {
                    order.push("has");
                    return function (v) { order.push("has(" + v + ")"); return v === "a"; };
                },
                get keys() {
                    order.push("keys");
                    return function () {
                        order.push("keys()");
                        var vals = ["a", "b"], idx = 0;
                        return {
                            get next() {
                                order.push("next");
                                return function () {
                                    order.push("next()");
                                    return {
                                        get done() { order.push("done"); return idx >= vals.length; },
                                        get value() { order.push("value"); return vals[idx++]; }
                                    };
                                };
                            }
                        };
                    };
                }
            };
            var interSmall = new Set(["a", "c"]).intersection(sl);
            check("order:inter-le", eqArr(arr(interSmall), ["a"]) &&
                order.join(",") === "size,ToNumber,has,keys,has(a),has(c)");
            order = [];
            var interLarge = new Set(["a", "c", "d", "e"]).intersection(sl);
            check("order:inter-gt", eqArr(arr(interLarge), ["a"]) &&
                order.join(",") === "size,ToNumber,has,keys,keys(),next,next(),done,value,next(),done,value,next(),done");

            // 5. -0 canonicalization on both paths + non-boolean truthiness.
            var seenHas = [];
            var rHas = new Set([-0]).intersection({
                size: 1,
                has: function (v) { seenHas.push(v); return {}; },
                keys: function () { return [].values(); }
            });
            check("neg0:has-arg-is-pos0", seenHas.length === 1 && Object.is(seenHas[0], +0) && Object.is(arr(rHas)[0], +0));
            var rKeys = new Set([0, 1]).intersection({
                size: 1,
                has: function () { throw new Error("should not call has"); },
                keys: function () { return [-0].values(); }
            });
            check("neg0:keys-val-is-pos0", eqArr(arr(rKeys), [+0]) && Object.is(arr(rKeys)[0], +0));

            // 6. Live mutation + cursor compaction under >= 32 tombstones.
            var mut = new Set(["a", "b", "c", "d", "e"]);
            var mutRes = mut.symmetricDifference({
                size: 4,
                get has() { mut.add("q"); return function () { throw new Error("unexpected has"); }; },
                keys: function () {
                    var vals = ["x", "b", "c", "c"], idx = 0;
                    return {
                        next: function () {
                            if (idx === 0) {
                                mut.delete("b");
                                mut.delete("c");
                                mut.add("b");
                                mut.add("d");
                            }
                            return { done: idx >= vals.length, value: vals[idx++] };
                        }
                    };
                }
            });
            check("symDiff:live-mutation", eqArr(arr(mutRes), ["a", "c", "d", "e", "q", "x"]) &&
                eqArr(arr(mut), ["a", "d", "e", "q", "b"]));

            var big = new Set();
            for (var n = 0; n < 60; n++) big.add(n);
            var visited = [];
            var bigInter = big.intersection({
                size: 100,
                has: function (v) {
                    visited.push(v);
                    if (v === 0) {
                        for (var d = 1; d < 45; d++) big.delete(d);
                        big.add(999);
                    }
                    return v === 0 || v === 59 || v === 999;
                },
                keys: function () { return [].values(); }
            });
            check("cursor:compact", visited.length === 17 && eqArr(arr(bigInter), [0, 59, 999]));

            // 7. IteratorClose on early false return only; never on next() throws.
            var closeLog = [];
            var supIter = {
                i: 0,
                next: function () {
                    var vals = [1, 99, 2];
                    return this.i < vals.length ? { done: false, value: vals[this.i++] } : { done: true };
                },
                return: function () { closeLog.push("closed"); return {}; }
            };
            check("isSupersetOf:early-close",
                  new Set([1, 2, 3]).isSupersetOf({ size: 2, has: function () {}, keys: function () { return supIter; } }) === false &&
                  closeLog.length === 1);

            var genClosed = 0;
            function* genDisjoint() {
                try { yield 7; yield 2; yield 8; } finally { genClosed++; }
            }
            check("isDisjointFrom:gen-close",
                  new Set([1, 2, 3, 4]).isDisjointFrom({ size: 3, has: function () {}, keys: genDisjoint }) === false &&
                  genClosed === 1);

            var errClosed = 0;
            check("next-throw:no-close", throwsKind(RangeError, function () {
                new Set([1, 2]).isSupersetOf({
                    size: 1,
                    has: function () {},
                    keys: function () {
                        return {
                            next: function () { throw new RangeError("boom"); },
                            return: function () { errClosed++; return {}; }
                        };
                    }
                });
            }) && errClosed === 0);

            check("return-non-obj:throws", throwsKind(TypeError, function () {
                new Set([1, 2]).isSupersetOf({
                    size: 1,
                    has: function () {},
                    keys: function () {
                        return {
                            next: function () { return { done: false, value: 9 }; },
                            return: function () { return 42; }
                        };
                    }
                });
            }));

            // 8. Subclass isolation + GC liveness across callbacks.
            var speciesRead = 0;
            class SubSet extends Set {
                static get [Symbol.species]() { speciesRead++; return SubSet; }
                get size() { throw new Error("receiver size called"); }
                has() { throw new Error("receiver has called"); }
                keys() { throw new Error("receiver keys called"); }
            }
            var origAdd = Set.prototype.add;
            var addCalled = 0;
            var sOther = new Set([2, 3]);
            var sSub = new SubSet([1, 2]);
            Set.prototype.add = function () { addCalled++; return origAdd.apply(this, arguments); };
            var uOut = sSub.union(sOther);
            Set.prototype.add = origAdd;
            check("subclass:plain-Set", (uOut instanceof Set) && !(uOut instanceof SubSet) &&
                Object.getPrototypeOf(uOut) === Set.prototype &&
                speciesRead === 0 && addCalled === 0 && eqArr(arr(uOut), [1, 2, 3]));

            var gcRes = new Set([{ keep: 1 }]).union({
                size: 2,
                has: function () { return false; },
                keys: function () {
                    var items = [{ fresh: 2 }, { fresh: 3 }], idx = 0;
                    return {
                        next: function () {
                            forceGc();
                            if (idx >= items.length) return { done: true };
                            var val = items[idx++];
                            forceGc();
                            return { done: false, value: val };
                        }
                    };
                }
            });
            forceGc();
            var gcArr = arr(gcRes);
            check("gc:entries-alive", gcArr.length === 3 && gcArr[0].keep === 1 && gcArr[1].fresh === 2 && gcArr[2].fresh === 3);

            return failures.length ? failures.join("\n") : "ok";
        })();
    )JS");
    assert(!page.js.last_eval_failed());
    if (result.to_string() != "ok") {
        std::fprintf(stderr, "set_prototype_es2025_set_methods failures:\n%s\n", result.to_string().c_str());
    }
    assert(result.to_string() == "ok");
}
