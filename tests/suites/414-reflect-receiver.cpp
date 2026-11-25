// The receiver `Reflect.get` and `Reflect.set` are given is the one used.
//
// §28.1.6 and §28.1.13 are not "read the property" and "write the property":
// they are `target.[[Get]](key, receiver)` and `target.[[Set]](key, value,
// receiver)`, and the receiver is the third and fourth argument. This engine
// ignored it — `Reflect.get({get x(){return this.y;}}, "x", {y: 9})` answered
// `undefined` where Node answers 9 — because the method called
// `get_property`/`set_property` with the target alone.
//
// §28.1.13 also *answers* the `Set` result instead of throwing, which is the
// whole difference from an assignment in strict code. The old code returned
// `true` unconditionally, so a write to a frozen object reported success when
// the standard says `false`.
//
// Found by running the same expressions in both engines and diffing them —
// `Reflect.set`'s receiver is the one case a "does it write at all" test never
// reaches, because the write to the target succeeds either way.
//
// Every expected value below is Node v22.22.3's, by evaluating the same
// expression there.
TEST(reflect_uses_the_receiver_it_is_given) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // ---- Reflect.get ------------------------------------------------------
    // A getter sees the receiver as `this`, not the object it was found on.
    assert(run("Reflect.get({get x(){return this.y;}}, 'x', {y: 9})") == "9");
    // With no third argument the receiver is the target (step 1's default).
    assert(run("Reflect.get({get x(){return this === undefined ? 'no' : 'yes';}}, 'x')") == "yes");
    // A primitive receiver is the `this` a sloppy getter is boxed from.
    assert(run("Reflect.get({get x(){return typeof this;}}, 'x', 42)") == "object");
    // An owned data property is read from the target, wherever the receiver is.
    assert(run("Reflect.get({x: 1}, 'x', {x: 9})") == "1");
    // An inherited one is found by walking, and the receiver still wins for
    // what the getter sees.
    assert(run("(function(){var p = {get g(){return this.tag;}};"
               "var o = Object.create(p);"
               "return Reflect.get(o, 'g', {tag: 'receiver'});})()") == "receiver");
    // A symbol key works, and the property is not converted to a string.
    assert(run("(function(){var s = Symbol('k'); var o = {}; o[s] = 7;"
               "return Reflect.get(o, s);})()") == "7");

    // ---- Reflect.set: the receiver is where an inherited setter writes -----
    assert(run("(function(){var o = {set x(v){this.y = v;}}; var r = {};"
               "Reflect.set(o, 'x', 5, r); return r.y + ',' + String(o.y);})()")
           == "5,undefined");
    // With no fourth argument it is the target, so the setter sees the target.
    assert(run("(function(){var o = {set x(v){this.y = v;}};"
               "Reflect.set(o, 'x', 5); return o.y;})()") == "5");

    // ---- Reflect.set answers the [[Set]] result ---------------------------
    // A refused write is `false`, not an exception: §28.1.13 step 2 returns
    // the boolean the ordinary `Set` produced.
    assert(run("Reflect.set(Object.freeze({}), 'x', 1)") == "false");
    assert(run("Reflect.set({}, 'x', 1)") == "true");
    assert(run("(function(){var o = {}; Object.defineProperty(o, 'x', {value: 1, writable: false});"
               "return Reflect.set(o, 'x', 2);})()") == "false");
    // An accessor with no setter refuses too, and does not throw.
    assert(run("(function(){var o = {get x(){return 1;}};"
               "return Reflect.set(o, 'x', 2);})()") == "false");
    // An array's frozen index, and a write past a sealed length.
    assert(run("(function(){var a = Object.freeze([1,2]);"
               "return Reflect.set(a, '0', 9);})()") == "false");

    // ---- and the ordinary behaviour this shares with the rest of the engine
    // A setter that recurses into an ordinary assignment still sees the object
    // it was assigned on: opening a receiver must not change that.
    assert(run("(function(){var o = {set x(v){this.base = v * 2;}};"
               "o.x = 4; return o.base;})()") == "8");
    // A plain assignment to a getter-only property is still silent in sloppy
    // code, and the value is unchanged.
    assert(run("(function(){var o = {get x(){return 1;}}; o.x = 2; return o.x;})()") == "1");
    // Object.assign uses the same machinery and must still copy.
    assert(run("JSON.stringify(Object.assign({}, {a: 1, b: 2}))") == "{\"a\":1,\"b\":2}");
}
