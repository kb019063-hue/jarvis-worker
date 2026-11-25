// Phase 444: numeric parsers trim the full ECMAScript WhiteSpace set.
TEST(parse_numeric_trims_ecmascript_unicode_whitespace) {
    TestPage page;
    JsInterpreter& js = page.js;
    assert(js.eval("[parseInt('\\u1680-42tail'),parseInt(' \\u205f0x10tail'),"
                   "parseInt('\\u3000\\ufeff+17'),parseFloat('\\u1680-42.5tail'),"
                   "parseFloat('\\u205f-Infinityx'),Number.parseFloat('\\u3000+12.5e2x')]"
                   ".join('/')").to_string() == "-42/16/17/-42.5/-Infinity/1250");
    assert(js.eval("[1/parseInt('\\u1680-0'),1/parseFloat('\\u3000-0.0'),"
                   "parseInt('\\u1680\\u205f\\u3000\\ufeff42'),"
                   "parseFloat('1\\u16802')].join('/')")
               .to_string() == "-Infinity/-Infinity/42/1");
    assert(js.eval("isNaN(parseInt('\\u180e42')) && isNaN(parseFloat('\\u008542')) && "
                   "isNaN(parseInt('\\u200b42')) && isNaN(parseFloat('\\u300142'))")
               .to_string() == "true");
}
