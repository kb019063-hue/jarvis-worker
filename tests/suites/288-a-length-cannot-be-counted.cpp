// Phase 288, finding 293: a length names places nobody wrote.
//
// An array's `length` is the count of places, not of storage. This engine used
// to answer every length assignment by materialising one 88-byte `JsValue` per
// place, so `[].length = 1e8` asked for 8.8 GiB: either the process was killed
// or `std::bad_alloc` escaped to script. Node answers the same assignment
// instantly, and `new Array(1e8)` too, because a hole is not storage.
TEST(a_length_cannot_be_counted) {
    using namespace litebird;
    TestPage page;
    JsInterpreter& js = page.js;
    auto text = [&](const std::string& source) { return js.eval(source).to_string(); };

    // ---- the three that were wrong, each read from Node v22.22.3 ----------
    // A length past every place that exists stores nothing, and reads back.
    assert(text("(function(){ var a = []; a.length = 100000000; return String(a.length); })()") ==
           "100000000");
    // The maximum a length may be: 2^32 - 1. This threw `std::bad_alloc`.
    assert(text("(function(){ var a = []; a.length = 4294967295; return String(a.length); })()") ==
           "4294967295");
    // Past that it is the language's RangeError, not a C++ exception.
    assert(text("(function(){ try { var a = []; a.length = 4294967296; return 'accepted'; }"
                "  catch (e) { return e.name; } })()") == "RangeError");
    // `new Array(n)` used to refuse anything past 2^26 with a limit invented
    // in this engine. A hole is not storage, so there is nothing to refuse.
    assert(text("String(new Array(100000000).length)") == "100000000");
    // The places it names are holes: no keys, not `in`, and they read undefined.
    assert(text("(function(){ var a = new Array(100000000);"
                "  return Object.keys(a).length + '/' + (0 in a) + '/' + String(a[0]); })()") ==
           "0/false/undefined");

    // ---- a length that names places, and the places that are there ---------
    // Writing inside such an array stores the one place and nothing between.
    assert(text("(function(){ var a = []; a.length = 1000000; a[500000] = 7;"
                "  return a[500000] + '/' + a.length + '/' + Object.keys(a).length; })()") ==
           "7/1000000/1");
    // Far past the elements too, which used to be a RangeError of its own.
    assert(text("(function(){ var a = []; a[99999999] = 1;"
                "  return a.length + '/' + a[99999999] + '/' + Object.keys(a).length; })()") ==
           "100000000/1/1");
    // An index just past the length extends it, as it always did.
    assert(text("(function(){ var a = []; a.length = 3; a[3] = 26; return a.length; })()") == "4");

    // ---- shrinking still loses places, and the count follows --------------
    assert(text("(function(){ var a = []; a.length = 10; a[2] = 'x'; a.length = 3;"
                "  return a.length + '/' + Object.keys(a).join(','); })()") == "3/2");
    assert(text("(function(){ var a = []; a.length = 10; a[2] = 'x'; a.length = 1;"
                "  return a.length + '/' + Object.keys(a).length; })()") == "1/0");
    // Truncate then push: the count is the length again, and `push` sees it.
    assert(text("(function(){ var log = [1,2,3]; log.length = 0; log.push('a');"
                "  return log.length + '/' + log[0]; })()") == "1/a");

    // ---- what a length that names places does to everything else -----------
    // JSON writes each place, holes as null — the length, not the storage.
    assert(text("JSON.stringify(new Array(5))") == "[null,null,null,null,null]");
    assert(text("(function(){ var a = []; a.length = 10; return a.join(','); })()") == ",,,,,,,,,");
    // Spread and `Array.from` yield every place, a hole as undefined.
    assert(text("(function(){ var a = []; a.length = 5; return [...a].length + '/' +"
                " Array.from(a).length; })()") == "5/5");
    // The descriptor's value is the count, whatever the storage holds.
    assert(text("(function(){ var a = []; a.length = 10; var d = Object.getOwnPropertyDescriptor(a, 'length');"
                "  return d.value + '/' + d.writable + '/' + d.enumerable + '/' + d.configurable; })()") ==
           "10/true/false/false");
    // And it is still one key, not two.
    assert(text("(function(){ var a = []; a.length = 3; a.b = 1;"
                "  return Object.getOwnPropertyNames(a).join(','); })()") == "length,b");
    // push and pop work off the length, not the element count.
    // `pop` takes the place just pushed, not one of the ten holes: writing
    // below answered `undefined/9` here until this assertion was checked
    // against Node, which says the 1 is what was pushed and so what pops.
    assert(text("(function(){ var a = []; a.length = 10;"
                "  return a.push(1) + '/' + a.length + '/' + a.pop() + '/' + a.length; })()") ==
           "11/11/1/10");

    // ---- the iterator still notices an array that grows under it ----------
    // `length` is asked again each step: `visit` runs script, and the array
    // can be pushed to while the loop is inside it.
    assert(text("(function(){ var grow = [1,2,3]; var got = [];"
                "  for (var x of grow) { got.push(x); if (x === 1) grow.push(4); }"
                "  return got.join(); })()") == "1,2,3,4");
}
