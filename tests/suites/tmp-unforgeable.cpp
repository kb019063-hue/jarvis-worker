// The Window's [LegacyUnforgeable] members belong to the window itself.
//
// HTML declares four members of `Window` [LegacyUnforgeable]:
//
//     [LegacyUnforgeable] readonly attribute WindowProxy window;
//     [LegacyUnforgeable] readonly attribute Document document;
//     [PutForwards=href, LegacyUnforgeable] readonly attribute Location location;
//     [LegacyUnforgeable] readonly attribute WindowProxy? top;
//
// WebIDL §3.4.10 says of such a member that "the property will be
// non-configurable and will exist as an own property on the object itself
// rather than on its prototype", and §3.7.6 gives every attribute the
// descriptor `{[[Enumerable]]: true, [[Configurable]]: false}` (false exactly
// when the attribute is unforgeable). §3.7.6 also says that for an interface
// declared `[Global]` — and `Window` is — attributes are exposed on the object
// itself, never on the prototype.
//
// The engine had all four wrong in one of two ways. `window` and `document`
// were own *data* properties, enumerable: false, configurable: true, so
// `delete globalThis.window` answered `true` and a page could destroy its own
// `window` binding. `location` and `top` were not own at all — only accessors
// on `Window.prototype` — so they were invisible to `getOwnPropertyNames`,
// absent from `Object.keys`, and deletable. The two halves are independent:
// a fix that only marks the bindings permanent fixes `window`/`document` and
// leaves `location`/`top` on the prototype.
//
// What makes the attribute unforgeable is the non-configurable own property:
// it is what a page would have to replace to forge `document`, and §3.4.10's
// own example is exactly that attack. `[[Delete]]` answering `false` and
// `[[DefineOwnProperty]]` throwing a TypeError both follow from it, and are
// the two ways a script notices.
//
// `self` is [Replaceable], not [LegacyUnforgeable], and must stay deletable;
// `globalThis` likewise (§18.1.5). Both are asserted here so a fix that
// marks the whole Window object permanent is caught.
//
// Every expected value below was checked against Node v22.22.3 by defining a
// non-configurable own accessor of the same name on `globalThis` and
// evaluating the same expression (`/tmp/oracle.js`, `/tmp/oracle2.js`): the
// descriptor is `{enumerable: true, configurable: false, get: fn, set:
// undefined}`, `delete` is `false`, `var n = 1` leaves the accessor's value,
// and `Object.defineProperty` throws a TypeError.
TEST(the_window_unforgeable_members_are_own_and_non_configurable) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // The shape, for all four at once. A descriptor that is a data property,
    // or configurable, or not enumerable is the old behaviour.
    assert(run("var bad = [];"
               "['window','document','location','top'].forEach(function (n) {"
               "  var d = Object.getOwnPropertyDescriptor(globalThis, n);"
               "  if (!d) { bad.push(n + ':absent'); return; }"
               "  if (!('get' in d)) bad.push(n + ':data');"
               "  else if (d.configurable) bad.push(n + ':configurable');"
               "  else if (!d.enumerable) bad.push(n + ':not-enumerable');"
               // `location` is [PutForwards=href], so it has a setter; the
               // other three are readonly and must not.
               "  else if (n !== 'location' && d.set !== undefined) bad.push(n + ':setter');"
               "});"
               "bad.join(',') || 'ok'") == "ok");

    // Own, not inherited: §3.4.10 says "on the object itself rather than on
    // its prototype". This is the half `location`/`top` failed.
    assert(run("['window','document','location','top']"
               ".filter(function (n) { return !Object.prototype.hasOwnProperty.call(globalThis, n); })"
               ".join(',') || 'none'") == "none");

    // They are still *accessors* on Window.prototype as well, which is what
    // §3.7.6 gives a [Global] interface's attributes. The own copy must not
    // have displaced the prototype entry or turned it into data.
    assert(run("var proto = Object.getPrototypeOf(globalThis);"
               "['window','document','location','top'].filter(function (n) {"
               "  var d = Object.getOwnPropertyDescriptor(proto, n);"
               "  return !d || !('get' in d);"
               "}).join(',') || 'all-accessors'") == "all-accessors");

    // Enumerable: they appear where a script enumerates. `location` and `top`
    // used to be missing from the own names entirely.
    assert(run("['window','document','location','top']"
               ".filter(function (n) { return Object.keys(globalThis).indexOf(n) < 0; })"
               ".join(',') || 'all-enumerable'") == "all-enumerable");
    assert(run("['window','document','location','top']"
               ".filter(function (n) { return Object.getOwnPropertyNames(globalThis).indexOf(n) < 0; })"
               ".join(',') || 'all-own'") == "all-own");

    // `[[Delete]]` is false and the value survives. The old engine answered
    // `true` for all four.
    assert(run("'window,document,location,top'.split(',').map(function (n) {"
               "  var gone = delete globalThis[n];"
               "  return gone + ':' + (globalThis[n] === undefined ? 'lost' : 'kept');"
               "}).join(' ')") == "false:kept false:kept false:kept false:kept");

    // `[[DefineOwnProperty]]` throws, which is the other half of
    // non-configurable and the attack §3.4.10 forbids.
    assert(run("'window,document,location,top'.split(',').map(function (n) {"
               "  try { Object.defineProperty(globalThis, n, {value: 'forged'}); return n + ':allowed'; }"
               "  catch (e) { return n + ':' + (e.name === 'TypeError' ? 'typeerror' : e.name); }"
               "}).join(' ')") == "window:typeerror document:typeerror location:typeerror top:typeerror");

    // [PutForwards=href]: assigning to `location` navigates, and is therefore
    // a property with a setter, unlike the three readonly unforgeables.
    assert(run("var d = Object.getOwnPropertyDescriptor(globalThis, 'location');"
               "typeof d.set") == "function");
    assert(run("['window','document','top'].filter(function (n) {"
               "return Object.getOwnPropertyDescriptor(globalThis, n).set !== undefined;"
               "}).length") == "0");

    // Nothing was actually forged, and the values are the real ones.
    assert(run("[window === globalThis, top === globalThis, document.nodeType,"
               " typeof location.href].join(',')") == "true,true,9,string");
    assert(run("document.defaultView === window") == "true");
}

// The neighbours that must *not* be made permanent.
//
// `self` and `frames` are [Replaceable] (§3.7.6's "regular attribute"),
// `globalThis` is §18.1.5's `{writable: true, enumerable: false,
// configurable: true}`, and a page-level `var` is §9.1.4's immutable binding.
// A fix written as "make every Window attribute non-configurable" would take
// all of these with it and no assertion above would notice.
TEST(the_window_members_that_are_not_unforgeable_stay_configurable) {
    using namespace litebird;

    TestPage page;
    JsInterpreter& js = page.js;
    const auto run = [&](const char* source) { return js.eval(source).to_string(); };

    // [Replaceable]: deleting is allowed, and the prototype getter shows
    // through afterwards. Node: `delete globalThis.self` is `true` for an
    // ordinary configurable property.
    assert(run("var d = Object.getOwnPropertyDescriptor(globalThis, 'self');"
               "[d ? d.configurable : 'absent', delete globalThis.self].join(',')")
           == "true,true");
    assert(run("typeof self") == "object");

    // §18.1.5.
    assert(run("Object.getOwnPropertyDescriptor(globalThis, 'globalThis').configurable") == "true");
    assert(run("delete globalThis.globalThis") == "true");

    // A page-level `var` is still non-configurable (§9.1.4), which was
    // Phase 431/436's business, not this one's; asserted so a fix here cannot
    // quietly relax it.
    assert(run("var keepMe = 1; Object.getOwnPropertyDescriptor(globalThis, 'keepMe').configurable") == "false");
    assert(run("delete globalThis.keepMe") == "false");

    // A `var` shadowing an unforgeable name is the behaviour the old engine
    // only appeared to support. Node, with a non-configurable own accessor
    // named `top`: `var top = 5; top` evaluates to the accessor's value,
    // without throwing. Sloppy assignment is likewise a silent no-op.
    assert(run("var top = 5; typeof top") == "object");
    assert(run("top = 5; typeof top") == "object");
}
