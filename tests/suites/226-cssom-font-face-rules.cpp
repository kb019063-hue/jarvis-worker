// Phase 226, finding 230: font-face rules and their live descriptors.
TEST(cssom_font_face_rules_have_live_descriptors) {
    using namespace litebird;
    TestPage page(TestPage::WithUrl{});
    JsInterpreter& js = page.js;
    js.load_page("<!doctype html><style>"
                 "@font-face { font-family: Test; src: local(Test); font-display: swap; color: red }"
                 "@media screen { @font-face { font-family: Nested } }"
                 "p { color: red }"
                 "</style><p id=target>text</p>");
    js.notify_document_loaded();
    js.eval(R"JS(
        var failures = [];
        function ok(label, got, want) {
            if (String(got) !== String(want)) failures.push(label + ': ' + got + ' != ' + want);
        }
        function err(fn) { try { fn(); return 'no throw'; } catch(e) { return e.name; } }
        var sheet = document.querySelector('style').sheet;
        var r = sheet.cssRules[0], st = r.style, m = sheet.cssRules[1];
        ok('count and order', sheet.cssRules.length, 3);
        ok('font face class', r instanceof CSSFontFaceRule, true);
        ok('base class', r instanceof CSSRule, true);
        ok('type', r.type, 5);
        ok('stable rule', r === sheet.cssRules.item(0), true);
        ok('stable style', st === r.style, true);
        ok('style parent', st.parentRule === r, true);
        ok('rule sheet', r.parentStyleSheet === sheet, true);
        ok('nested parent', m.cssRules[0].parentRule === m, true);
        ok('nested sheet', m.cssRules[0].parentStyleSheet === sheet, true);
        ok('declaration count', st.length, 3);
        ok('first name', st.item(0), 'font-family');
        ok('local serialization', st.src, 'local("Test")');
        ok('not a descriptor', st.getPropertyValue('color'), '');
        ok('rule text', r.cssText, '@font-face { font-family: Test; src: local("Test"); font-display: swap; }');
        ok('cascade unaffected', getComputedStyle(document.getElementById('target')).color, 'rgb(255, 0, 0)');
        st.fontDisplay = 'block';
        st.setProperty('font-weight', '100 900');
        st.setProperty('font-display', 'garbage');
        ok('validation', st.fontDisplay, 'block');
        ok('range', st.fontWeight, '100 900');
        st.setProperty('font-display', 'optional', 'important');
        sheet.insertRule('p { color: blue }', 3);
        ok('generation retains important', st.getPropertyPriority('font-display'), 'important');
        ok('generation retains value', st.fontDisplay, 'optional');
        ok('removal result', st.removeProperty('font-display'), 'optional');
        ok('removal visible', st.fontDisplay, '');
        st.cssText = 'font-family: Other; src: url(font.woff2); color: red; font-display: swap !important';
        ok('cssText filter', st.cssText, 'font-family: Other; src: url("font.woff2");');
        var index = sheet.insertRule('@font-face { font-family: Inserted }', 0);
        ok('insert index', index, 0);
        var inserted = sheet.cssRules[0], held = inserted.style;
        sheet.deleteRule(0);
        held.fontFamily = 'Detached';
        ok('detached retained', inserted.style.fontFamily, 'Detached');
        ok('invalid insert', err(function() { sheet.insertRule('@font-face x {}', 0); }), 'SyntaxError');
        ok('illegal receiver', err(function() { Object.getOwnPropertyDescriptor(CSSFontFaceRule.prototype,'style').get.call({}); }), 'TypeError');
        var other = new CSSStyleSheet();
        other.replaceSync('@font-face {}');
        var otherRule = other.cssRules[0];
        ok('identity across sheets', r === sheet.cssRules[0], true);
        ok('style parent across sheets', st.parentRule === r, true);
        ok('distinct sheets', otherRule !== r, true);
        ok('illegal constructor', err(function() { new CSSFontFaceRule(); }), 'TypeError');
    )JS");
    assert(js.eval("failures.join('; ')").to_string().empty());
}

