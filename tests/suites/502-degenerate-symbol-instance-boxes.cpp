// Phase 502: a non-rendering rect is not in an instanced symbol's box.
TEST(svg_symbol_degenerate_geometry_does_not_pollute_bbox) {
    TestPage page;
    auto S = [&](const char* code) { return page.js.eval(code).to_string(); };
    S("document.body.innerHTML='<svg width=\"200\" height=\"100\" viewBox=\"0 0 20 10\">"
      "<defs><rect id=\"zero\" x=\"1\" y=\"2\" width=\"0\" height=\"4\"/>"
      "<symbol id=\"pct\"><rect x=\"10%\" y=\"20%\" width=\"30%\" height=\"40%\"/></symbol>"
      "<symbol id=\"fixed\"><rect x=\"1\" y=\"2\" width=\"3\" height=\"4\"/></symbol>"
      "<symbol id=\"mix\"><g transform=\"translate(3,4)\"><use href=\"#zero\"/></g>"
      "<rect x=\"10\" y=\"10\" width=\"4\" height=\"5\"/></symbol>"
      "<symbol id=\"line\"><line x1=\"10%\" y1=\"20%\" x2=\"70%\" y2=\"80%\"/></symbol>"
      "<symbol id=\"point\"><line x1=\"0\" y1=\"0\" x2=\"0\" y2=\"0\"/></symbol>"
      "<symbol id=\"vb\" viewBox=\"0 0 10 10\"><rect x=\"10%\" y=\"20%\" width=\"30%\" height=\"40%\"/></symbol></defs>"
      "<use id=\"u\" x=\"5\" y=\"7\" href=\"#pct\" width=\"0\" height=\"10\"/></svg>'");
    auto box = [&](const char* id) {
        return S(("(function(){var b=document.getElementById('" + std::string(id) +
            "').getBBox();return [b.x,b.y,b.width,b.height].join(',')})()").c_str());
    };
    assert(box("u") == "0,0,0,0");
    S("var u=document.getElementById('u');u.setAttribute('width','20');u.setAttribute('height','0')");
    assert(box("u") == "0,0,0,0");
    S("u.setAttribute('width','-1');u.setAttribute('height','10')");
    assert(box("u") == "0,0,0,0");
    S("u.setAttribute('width','20');u.setAttribute('height','10')");
    assert(box("u") == "7,9,6,4");
    S("u.setAttribute('href','#fixed');u.setAttribute('width','0')");
    assert(box("u") == "6,9,3,4");
    S("u.setAttribute('href','#mix')");
    assert(box("u") == "15,17,4,5");
    S("u.setAttribute('href','#line');u.setAttribute('width','-1')");
    assert(box("u") == "5,9,0,6");
    S("u.setAttribute('width','0');u.setAttribute('height','0');u.setAttribute('href','#point')");
    assert(box("u") == "0,0,0,0");
    S("u.setAttribute('href','#vb');u.setAttribute('height','10')");
    assert(box("u") == "6,9,3,4");
    // Direct measurement retains Phase 433's zero-width rectangle box.
    assert(box("zero") == "1,2,0,4");
}
