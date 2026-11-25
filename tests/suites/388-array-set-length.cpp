// Phase 388, finding 388: ArraySetLength does what §10.4.2.4 says when a
// descriptor sets an array's `length` — it deletes the places above the new
// length, refuses when one cannot be deleted, and keeps `length`'s own
// attributes.
//
// Every assertion was compared against Node v22.22.3 first.
TEST(array_set_length_deletes_the_places_above_the_new_length) {
    TestPage page;
    JsInterpreter& js = page.js;

    // §10.4.2.4 step 4: the places at or above the new length are deleted, so
    // they stop being properties — `in` says false, `hasOwnProperty` says
    // false, and `length` is the new count.
    assert(js.eval(
               "var a = [0, 1, 2, 3];"
               "Object.defineProperty(a, 'length', { value: 2 });"
               "a.length + '/' + a.join() + '/' + (2 in a) + '/' + a.hasOwnProperty('3')")
               .to_string() == "2/0,1/false/false");

    // Growing through a descriptor adds holes, not values.
    assert(js.eval(
               "var a = [0];"
               "Object.defineProperty(a, 'length', { value: 4 });"
               "a.length + '/' + (1 in a) + '/' + (3 in a)")
               .to_string() == "4/false/false");

    // §10.4.2.4 step 4.b: a place that is not configurable cannot be deleted.
    // The request is refused with a TypeError, and — step 4.c.iii — the length
    // ends up just past the last place that would not go.
    assert(js.eval(
               "var a = [0, 1];"
               "Object.defineProperty(a, '1', { value: 1, configurable: false });"
               "(function () {"
               "  try { Object.defineProperty(a, 'length', { value: 0 }); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "TypeError");
    assert(js.eval(
               "var a = [0, 1];"
               "Object.defineProperty(a, '1', { value: 1, configurable: false });"
               "try { Object.defineProperty(a, 'length', { value: 0 }); } catch (e) {}"
               "a.length")
               .to_string() == "2");

    // The same request through `defineProperties`, which is where the family of
    // test262 failures lived: the descriptor goes through the same path, and
    // before this phase `Object.defineProperties(arr, { length: { value: 1 } })`
    // over a non-configurable `arr[1]` answered nothing at all.
    assert(js.eval(
               "var a = [0, 1];"
               "Object.defineProperty(a, '1', { value: 1, configurable: false });"
               "(function () {"
               "  try { Object.defineProperties(a, { length: { value: 1 } }); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "TypeError");
    assert(js.eval(
               "var a = [0, 1];"
               "Object.defineProperty(a, '1', { value: 1, configurable: false });"
               "try { Object.defineProperties(a, { length: { value: 1 } }); } catch (e) {}"
               "var d = Object.getOwnPropertyDescriptor(a, 'length');"
               "d.value + '/' + d.writable + '/' + d.enumerable + '/' + d.configurable")
               .to_string() == "2/true/false/false");

    // A configurable place at or above the new length *is* deleted, and the
    // request succeeds: the refusal is only for a place that will not go.
    assert(js.eval(
               "var a = [0, 1, 2];"
               "Object.defineProperty(a, '1', { value: 9, configurable: true });"
               "Object.defineProperty(a, 'length', { value: 1 });"
               "a.length + '/' + a.join() + '/' + (1 in a)")
               .to_string() == "1/0/false");

    // An index whose attributes went into the property map (a hole whose map
    // entry is the property) is deleted by the same sweep, not left behind.
    assert(js.eval(
               "var a = [0, 1, 2];"
               "Object.defineProperty(a, '2', { writable: false });"
               "Object.defineProperty(a, 'length', { value: 1 });"
               "(2 in a) + '/' + Object.getOwnPropertyDescriptor(a, '2')")
               .to_string() == "false/undefined");

    // §10.4.2.3 step 3.a.i: an array's `length` may not be made configurable,
    // and it is not enumerable or configurable to begin with.
    assert(js.eval(
               "(function () {"
               "  try { Object.defineProperty([], 'length', { configurable: true }); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "TypeError");
    assert(js.eval(
               "var a = [];"
               "Object.defineProperty(a, 'length', { writable: false });"
               "var d = Object.getOwnPropertyDescriptor(a, 'length');"
               "d.value + '/' + d.writable + '/' + d.enumerable + '/' + d.configurable")
               .to_string() == "0/false/false/false");

    // A non-writable `length` still refuses to grow, and `push` says so.
    assert(js.eval(
               "var a = [];"
               "Object.defineProperty(a, 'length', { writable: false });"
               "(function () { try { a.push(1); return 'pushed'; } catch (e) { return e.name; } })()")
               .to_string() == "TypeError");

    // An invalid length is a RangeError before any attribute is looked at
    // (Phase 249, finding 253), and that still holds.
    assert(js.eval(
               "(function () {"
               "  try { Object.defineProperty([], 'length', { value: -1 }); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "RangeError");
    assert(js.eval(
               "(function () {"
               "  try { Object.defineProperty([], 'length', { value: 1.5 }); return 'made'; }"
               "  catch (e) { return e.name; }"
               "})()")
               .to_string() == "RangeError");

    // A plain assignment to `length` still deletes too, and the two paths
    // agree; this is the behaviour Phase 249 built and this phase must not
    // have moved.
    assert(js.eval(
               "var a = [0, 1, 2, 3];"
               "a.length = 2;"
               "a.join() + '/' + (2 in a)")
               .to_string() == "0,1/false");

    // A non-array object's `length` is an ordinary property: nothing is
    // deleted and nothing is refused.
    assert(js.eval(
               "var o = { length: 5, 0: 'x', 1: 'y' };"
               "Object.defineProperty(o, 'length', { value: 1 });"
               "o.length + '/' + o[1] + '/' + Object.getOwnPropertyDescriptor(o, 'length').configurable")
               .to_string() == "1/y/true");
}
