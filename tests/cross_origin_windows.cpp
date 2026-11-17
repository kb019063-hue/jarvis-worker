// Cross-origin Window and Location reflection: what the parent realm of one
// origin can see of the window it embedded cross-origin (HTML §7.2.1,
// §7.2.3 and §7.2.4).
//
// A page at https://app.example.test embeds https://other.example.test/child.html,
// served from a temporary directory through ResourceLoader's origin roots, so
// the two realms are cross-origin and every path below goes through the
// cross-origin rules. The child embeds one srcdoc grandchild, so the indexed
// properties are there too.
//
// This suite is standalone because tests/test_suite.cpp is claimed by another
// open pull request; the Makefile runs it as part of `make test`.
//
// Oracle: the HTML Standard, sections 7.2.1.3 (CrossOriginProperties,
// CrossOriginGetOwnPropertyHelper, CrossOriginOwnPropertyKeys), 7.2.3
// (WindowProxy exotic object) and 7.2.4 (Location interface).

#include "litebird/dom/Document.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace litebird;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(const std::string& name, bool ok) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

void expect_eq(JsInterpreter& js, const std::string& name, const std::string& expr,
               const std::string& expected) {
    const std::string result = js.eval(expr).to_string();
    check(name + " (got \"" + result + "\")", result == expected);
}

void expect_ok(JsInterpreter& js, const std::string& name, const std::string& body) {
    const std::string code = "(function(){ try { var v = (" + body + "); return 'ok:' + String(v); }"
                             " catch (e) { return 'err:' + e.name; } })()";
    expect_eq(js, name, code, "ok:true");
}

void expect_err(JsInterpreter& js, const std::string& name, const std::string& body,
                const std::string& error_name) {
    const std::string code = "(function(){ try { var v = (" + body + "); return 'ok:' + String(v); }"
                             " catch (e) { return 'err:' + e.name; } })()";
    expect_eq(js, name, code, "err:" + error_name);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path child_dir = fs::temp_directory_path() / "litebird_cross_origin_windows";
    fs::create_directories(child_dir);
    {
        std::ofstream child(child_dir / "child.html");
        child << "<!doctype html><iframe name=\"gamma\" srcdoc=\"<p>leaf</p>\"></iframe>";
    }
    ResourceLoader::set_origin_root_for_testing("https://other.example.test", child_dir.string());

    auto doc = std::make_shared<Document>();
    doc->set_url("https://app.example.test/index.html");
    JsInterpreter js(doc);
    js.load_page(R"html(<!doctype html>
<iframe id="x" name="xframe" src="https://other.example.test/child.html"></iframe>
<iframe id="s" srcdoc="<p>same</p>"></iframe>
<script>
  window.w = document.getElementById('x').contentWindow;
  window.L = window.w.location;
  window.same = document.getElementById('s').contentWindow;
  window.d = (o, k) => Object.getOwnPropertyDescriptor(o, k);
</script>
)html", true);
    js.notify_document_loaded();
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));

    check("child window exists", js.eval("!!w").is_truthy());
    check("child has one child navigable", js.eval("w.length").to_number() == 1);

    // -- [[OwnPropertyKeys]] (HTML §7.2.3.10, §7.2.4.10) --------------------
    const std::string win_list = "0,window,self,location,close,closed,focus,blur,frames,length,"
                                 "top,opener,parent,postMessage,then";
    expect_eq(js, "Object.keys(win)", "Object.keys(w).join(',')", "0");
    expect_eq(js, "getOwnPropertyNames(win)", "Object.getOwnPropertyNames(w).join(',')", win_list);
    expect_eq(js, "Reflect.ownKeys(win)", "Reflect.ownKeys(w).join(',')", win_list);
    expect_eq(js, "Object.keys(loc)", "Object.keys(L).join(',')", "");
    expect_eq(js, "getOwnPropertyNames(loc)", "Object.getOwnPropertyNames(L).join(',')",
              "href,replace,then");
    expect_eq(js, "Reflect.ownKeys(loc)", "Reflect.ownKeys(L).join(',')", "href,replace,then");
    expect_eq(js, "for-in sees only the indices",
              "(function(){ var k = []; for (var key in w) k.push(key); return k.join(','); })()",
              "0");
    expect_eq(js, "object spread of win copies only the indices",
              "Object.keys({...w}).join(',')", "0");
    expect_eq(js, "object spread of loc copies nothing", "Object.keys({...L}).join(',')", "");
    expect_eq(js, "Object.assign copies only the indices",
              "Object.keys(Object.assign({}, w)).join(',')", "0");
    expect_eq(js, "Object.assign of loc copies nothing",
              "JSON.stringify(Object.assign({}, L))", "{}");

    // -- Descriptors (HTML §7.2.3.5, §7.2.1.3.4) ----------------------------
    expect_ok(js, "index descriptor is enumerable", "d(w, '0').enumerable === true");
    expect_ok(js, "index descriptor is configurable", "d(w, '0').configurable === true");
    expect_ok(js, "index descriptor is not writable", "d(w, '0').writable === false");
    expect_ok(js, "index descriptor holds the child proxy", "typeof d(w, '0').value === 'object'");
    expect_ok(js, "index descriptor value is the child window",
              "d(w, '0').value === w[0]");
    expect_ok(js, "'window' descriptor has a getter", "typeof d(w, 'window').get === 'function'");
    expect_ok(js, "'window' descriptor has no setter", "d(w, 'window').set === undefined");
    expect_ok(js, "'window' descriptor is not enumerable", "d(w, 'window').enumerable === false");
    expect_ok(js, "'window' descriptor is configurable", "d(w, 'window').configurable === true");
    expect_ok(js, "'window' getter returns the window", "d(w, 'window').get() === w");
    expect_ok(js, "'length' getter returns the child count", "d(w, 'length').get() === 1");
    expect_ok(js, "'location' descriptor has a getter",
              "typeof d(w, 'location').get === 'function'");
    expect_ok(js, "'location' descriptor has a setter",
              "typeof d(w, 'location').set === 'function'");
    expect_ok(js, "'close' descriptor is a data descriptor",
              "typeof d(w, 'close').value === 'function'");
    expect_ok(js, "'close' descriptor is not writable", "d(w, 'close').writable === false");
    expect_ok(js, "'close' descriptor is configurable", "d(w, 'close').configurable === true");
    expect_ok(js, "'then' descriptor is not enumerable", "d(w, 'then').enumerable === false");
    expect_ok(js, "'then' descriptor is configurable", "d(w, 'then').configurable === true");
    expect_ok(js, "'then' descriptor value is undefined", "d(w, 'then').value === undefined");
    expect_eq(js, "'document' descriptor does not exist",
              "String(d(w, 'document'))", "undefined");
    expect_eq(js, "'pathname' descriptor does not exist on loc",
              "String(d(L, 'pathname'))", "undefined");

    // -- Location descriptors (HTML §7.2.4.5) --------------------------------
    expect_eq(js, "'href' descriptor has no getter cross-origin",
              "String(d(L, 'href').get)", "undefined");
    expect_ok(js, "'href' descriptor has a setter", "typeof d(L, 'href').set === 'function'");
    expect_ok(js, "'href' descriptor is not enumerable", "d(L, 'href').enumerable === false");
    expect_ok(js, "'href' descriptor is configurable", "d(L, 'href').configurable === true");
    expect_ok(js, "'replace' descriptor is a data descriptor",
              "typeof d(L, 'replace').value === 'function'");
    expect_ok(js, "'replace' descriptor is configurable",
              "d(L, 'replace').configurable === true");

    // -- Get/set/delete security (HTML §7.2.3.7–9, §7.2.4.7–9) ---------------
    expect_err(js, "reading an unexposed property throws", "w.document", "SecurityError");
    expect_err(js, "reading loc.href throws", "L.href", "SecurityError");
    expect_err(js, "reading loc.pathname throws", "L.pathname", "SecurityError");
    expect_err(js, "reading loc.origin throws", "L.origin", "SecurityError");
    expect_err(js, "setting an unexposed property throws", "w.name = 'pwned'", "SecurityError");
    expect_err(js, "setting loc.pathname throws", "L.pathname = '/other'", "SecurityError");
    expect_err(js, "delete on win throws", "delete w.close", "SecurityError");
    expect_err(js, "delete on loc throws", "delete L.href", "SecurityError");
    expect_ok(js, "w.window reads through", "w.window === w");
    expect_ok(js, "w.self reads through", "w.self === w");
    expect_ok(js, "w.frames reads through", "w.frames === w");
    expect_ok(js, "w.closed reads through", "w.closed === false");
    expect_ok(js, "w.length reads through", "w.length === 1");
    expect_ok(js, "w.top is the parent", "w.top === window");
    expect_ok(js, "w.parent is the parent", "w.parent === window");
    expect_ok(js, "named child navigable is visible", "'gamma' in w");
    expect_ok(js, "other names are not visible", "!('someid' in w)");
    expect_ok(js, "'close' in w", "'close' in w");
    expect_ok(js, "'then' in w", "'then' in w");
    // `w.hasOwnProperty(...)` itself throws cross-origin (reading the method
    // is not exposed); call it through Object.prototype instead.
    expect_ok(js, "hasOwnProperty('close')",
              "Object.prototype.hasOwnProperty.call(w, 'close') === true");
    expect_ok(js, "hasOwnProperty('document')",
              "Object.prototype.hasOwnProperty.call(w, 'document') === false");
    expect_ok(js, "hasOwnProperty('href') on loc",
              "Object.prototype.hasOwnProperty.call(L, 'href') === true");
    expect_ok(js, "hasOwnProperty('reload') on loc",
              "Object.prototype.hasOwnProperty.call(L, 'reload') === false");
    expect_err(js, "JSON.stringify(win) throws", "JSON.stringify(w)", "SecurityError");

    // -- [[DefineOwnProperty]] refuses (HTML §7.2.3.6, §7.2.4.6) --------------
    expect_err(js, "defineProperty(win) throws",
               "Object.defineProperty(w, 'zzz', {value: 1})", "SecurityError");
    expect_err(js, "defineProperty(loc) throws",
               "Object.defineProperty(L, 'zzz', {value: 1})", "SecurityError");
    expect_err(js, "defineProperty on an index throws",
               "Object.defineProperty(w, '0', {value: 1})", "SecurityError");
    expect_err(js, "Reflect.defineProperty on win throws",
               "Reflect.defineProperty(w, 'zzz', {value: 1})", "SecurityError");

    // -- Integrity levels refuse (HTML §7.2.3.4, §7.2.4.4) --------------------
    expect_err(js, "Object.freeze(win) throws", "Object.freeze(w)", "TypeError");
    expect_err(js, "Object.seal(loc) throws", "Object.seal(L)", "TypeError");
    expect_err(js, "Object.preventExtensions(win) throws",
               "Object.preventExtensions(w)", "TypeError");
    expect_err(js, "Object.setPrototypeOf(win) throws",
               "Object.setPrototypeOf(w, null)", "TypeError");
    expect_ok(js, "Reflect.setPrototypeOf(win) reports false",
              "Reflect.setPrototypeOf(w, null) === false");
    expect_eq(js, "Object.getPrototypeOf(win) is null",
              "String(Object.getPrototypeOf(w))", "null");
    expect_eq(js, "Reflect.getPrototypeOf(win) is null",
              "String(Reflect.getPrototypeOf(w))", "null");
    expect_eq(js, "Object.getPrototypeOf(loc) is null",
              "String(Object.getPrototypeOf(L))", "null");
    expect_ok(js, "win is not instanceof anything", "!(w instanceof Object)");

    // -- Same-origin control: the ordinary path is untouched ------------------
    expect_ok(js, "same-origin child sees its document",
              "typeof same.document === 'object' && same.document !== null");
    expect_ok(js, "same-origin child has a prototype",
              "Object.getPrototypeOf(same) !== null");
    expect_ok(js, "same-origin child finds 'document' in the chain",
              "'document' in same");

    ResourceLoader::clear_origin_roots_for_testing();
    std::error_code ec;
    fs::remove_all(child_dir, ec);

    std::cout << "Cross-origin Window/Location suite: " << g_checks << " checks, "
              << g_failures << " failure(s).\n";
    return g_failures == 0 ? 0 : 1;
}
