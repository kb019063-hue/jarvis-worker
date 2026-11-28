// Phase 481: getBBox on a <use> instancing a <symbol> needs its viewport.
TEST(svg_symbol_instance_bbox) {
    TestPage page;
    auto S = [&](const char* code) { return page.js.eval(code).to_string(); };
    S("document.body.innerHTML='<svg width=\"200\" height=\"100\" viewBox=\"0 0 20 10\">"
      "<defs><symbol id=\"sym\" viewBox=\"0 0 10 10\">"
      "<g transform=\"translate(1,2)\"><rect width=\"30%\" height=\"40%\"/></g></symbol>"
      "<symbol id=\"plain\"><rect x=\"1\" y=\"2\" width=\"3\" height=\"4\"/></symbol>"
      "<symbol id=\"cycle\"><use href=\"#cycle\"/></symbol></defs>"
      "<g id=\"outer\"><use id=\"u\" href=\"#sym\" x=\"5\" y=\"7\" width=\"20\" height=\"10\" transform=\"translate(100,0)\"/></g>"
      "<use id=\"v\" href=\"#plain\" x=\"5\" y=\"7\" width=\"20\" height=\"10\"/>"
      "<use id=\"loop\" href=\"#cycle\"/></svg>'");
    auto box = [&](const char* id) {
        return S(("(function(){var b=document.getElementById('" + std::string(id) +
            "').getBBox();return [b.x,b.y,b.width,b.height].join(',')})()").c_str());
    };
    assert(box("u") == "11,9,3,4");
    assert(box("outer") == "111,9,3,4");
    assert(box("v") == "6,9,3,4");
    assert(box("loop") == "0,0,0,0");
    S("document.getElementById('sym').setAttribute('preserveAspectRatio','none')");
    assert(box("u") == "7,9,6,4");
    S("document.getElementById('sym').setAttribute('preserveAspectRatio','xMaxYMax slice')");
    assert(box("u") == "7,1,6,8");
    S("document.getElementById('sym').removeAttribute('preserveAspectRatio');"
      "document.getElementById('u').setAttribute('width','0')");
    // A zero viewport skips the mapping, but percentages still resolve against
    // the symbol's viewBox, not against the zero width.
    assert(box("u") == "6,9,3,4");
    S("document.getElementById('u').setAttribute('href','#plain')");
    assert(box("u") == "6,9,3,4");
}
