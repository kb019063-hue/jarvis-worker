## 196. `[1,2].toLocaleString()` never asked an element, and neither did `Object.prototype`'s

`Array.prototype.toLocaleString` was `join_array(receiver, ",")`. A script could replace `Number.prototype.toLocaleString` and the array formatted the numbers itself: `[1,2]` was `"1,2"`, `[1,,3]` was `"1,,3"`, and a hole was joined as a hole rather than skipped as `undefined`. `Object.prototype.toLocaleString` was `js_to_string(self())`, which is `"[object Object]"` for every object and looks nothing up.

§23.1.3.28 now walks the places, reads each with `Get`, and for anything that is not `undefined` or `null` invokes the element's `toLocaleString` with the element as it was read — no ToObject, so a strict method sees `"number"`. §20.1.3.5 is `Invoke(this, "toString")`, and now calls what the receiver's `toString` answers.

`python3 tools/conformance/run_test262.py --binary before=MAIN --binary now=build/litebird built-ins/Array built-ins/TypedArray` prints **3,375 → 3,380 pass** and **0 lost**; `built-ins/Array/prototype/toLocaleString` is 9/12, the three failures being the `BigInt` cases. `make -j2 test` prints **345/345**.

What is still open: no `toLocaleString` in the engine takes `locales` or `options`, so the array's takes none and passes none; the typed array keeps its own walker; the three `BigInt` cases need `BigInt`.
