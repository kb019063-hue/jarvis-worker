// Phase 452 — make_dom_exception asks the receiver's realm first.
// Covers the borrowed-method case the row describes.

TEST(dom_exception_uses_receiver_realm_when_borrowed) {
    using namespace litebird;
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));

    auto doc = std::make_shared<Document>();
    doc->set_url("https://dom-exception-452.test/caller.html");
    JsInterpreter js(doc);
    static const char* page = R"html(<!doctype html><html><body>
      <iframe id="f" src="data:text/xml,<html xmlns='http://www.w3.org/1999/xhtml'><foo/></html>"></iframe>
    </body></html>)html";
    js.load_page(page, true);
    js.notify_document_loaded();
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(5));

    // Two realms exist and their DOMExceptions differ.
    assert(js.eval("var w = document.getElementById('f').contentWindow;"
                   " var fd = document.getElementById('f').contentDocument;"
                   " w !== window && fd !== document").is_truthy());
    assert(js.eval("var foreignCtor = w.DOMException;"
                   " foreignCtor !== DOMException").is_truthy());

    // 1. Borrow the frame's createElement and call it with the local document
    //    as receiver: exception must be the receiver's (local), not the
    //    install realm's (foreign). Before the fix it was foreignCtor.
    assert(js.eval(R"js((function(){
        var foreignCreate = fd.createElement;
        try { foreignCreate.call(document, '1foo'); return 'no throw'; }
        catch (e) {
            if (e.name !== 'InvalidCharacterError') return 'name ' + e.name + ' ' + e.constructor;
            return e.constructor === DOMException && e.constructor !== foreignCtor ? 'ok' : 'wrong global';
        }
    })())js").to_string() == "ok");

    // 2. Same borrowed method but with frame document as receiver -> frame's.
    assert(js.eval(R"js((function(){
        var foreignCreate = w.document.createElement;
        try { foreignCreate.call(fd, '1foo'); return 'no throw'; }
        catch (e) {
            return e.constructor === foreignCtor && e.constructor !== DOMException ? 'ok' : 'wrong global';
        }
    })())js").to_string() == "ok");

    // 3. innerHTML setter borrowed cross-realm with XML non-well-formed fragment.
    assert(js.eval(R"js((function(){
        var localEl = document.createElement('div');
        // Take the setter via prototype to borrow it
        var setInner = Object.getOwnPropertyDescriptor(Element.prototype, 'innerHTML').set;
        // fd's Element.prototype setter is from w; borrowing it onto localEl
        // would be illegal invocation (receiver check), so test with accessible path:
        // use foreign document's element with its own setter via .call from page
        var fe = fd.createElement('el');
        var foreignSet = Object.getOwnPropertyDescriptor(w.Element.prototype, 'innerHTML').set;
        try { foreignSet.call(fe, 'x</foo--><!--y'); return 'no throw'; }
        catch (e) {
            // XML fragment in XML document throws SyntaxError from frame's realm
            if (e.name !== 'SyntaxError') return 'name ' + e.name;
            return e.constructor === foreignCtor ? 'ok' : 'wrong global';
        }
    })())js").to_string() == "ok");

    // 4. Normal (non-borrowed) local throw still local.
    assert(js.eval(R"js((function(){
        try { document.createElement('1foo'); return 'no throw'; }
        catch (e) { return e.constructor === DOMException ? 'ok' : 'wrong'; }
    })())js").to_string() == "ok");

    // 5. Normal frame throw still frame's.
    assert(js.eval(R"js((function(){
        try { fd.createElement('1foo'); return 'no throw'; }
        catch (e) { return e.constructor === foreignCtor ? 'ok' : 'wrong'; }
    })())js").to_string() == "ok");

    // 6. Storage-like non-node fallback: exception from a realm with no dom_node
    //    in current_this should still use the method's realm (smoke: no crash).
    assert(js.eval(R"js((function(){
        // new DOMException via w's constructor is w's
        var e = new w.DOMException('m', 'SyntaxError');
        return e.constructor === foreignCtor ? 'ok' : 'wrong';
    })())js").to_string() == "ok");

    // 7. throw_dom path already covered by 1-5 (wrap via make_dom_exception).

    js.eval("w = null; fd = null; foreignCtor = null;");
    EventLoop::default_loop().run_until_idle(std::chrono::seconds(1));
}
