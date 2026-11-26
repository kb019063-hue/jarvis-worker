// Phase 447: a use element's box includes its referenced geometry.
TEST(svg_use_bbox_follows_reference) {
    TestPage page;
    auto S = [&](const char* code) { return page.js.eval(code).to_string(); };
    S("document.body.innerHTML='<svg width=\"200\" height=\"100\">"
      "<defs><rect id=\"r\" x=\"1\" y=\"2\" width=\"3\" height=\"4\" transform=\"scale(2)\"/>"
      "<g id=\"source\"><rect x=\"2\" y=\"3\" width=\"4\" height=\"5\"/></g>"
      "<use id=\"cycle\" href=\"#cycle\"/></defs>"
      "<g id=\"outer\"><use id=\"u\" href=\"#r\" x=\"10\" y=\"20\" transform=\"translate(100,0)\"/></g>"
      "<use id=\"v\" href=\"#source\" x=\"4\" y=\"5\"/><use id=\"bad\" href=\"#cycle\"/></svg>'");
    auto box = [&](const char* id) {
        return S(("(function(){var b=document.getElementById('" + std::string(id) +
            "').getBBox();return [b.x,b.y,b.width,b.height].join(',')})()").c_str());
    };
    assert(box("u") == "12,24,6,8");
    assert(box("outer") == "112,24,6,8");
    assert(box("v") == "6,8,4,5");
    assert(box("bad") == "0,0,0,0");
    S("document.getElementById('r').setAttribute('width','10')");
    assert(box("u") == "12,24,20,8");
    S("document.getElementById('u').setAttribute('href','#missing')");
    assert(box("u") == "0,0,0,0");
}
