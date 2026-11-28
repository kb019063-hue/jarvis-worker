// Phase 525: <use> referencing an SVG element needs its instance viewport.
TEST(svg_use_referenced_viewport_bbox) {
    TestPage page;
    auto S = [&](const char* code) { return page.js.eval(code).to_string(); };
    S("document.body.innerHTML='<svg width=\"200\" height=\"100\" viewBox=\"0 0 20 10\">"
      "<defs><svg id=\"source\" x=\"2\" y=\"3\" width=\"20\" height=\"10\" viewBox=\"0 0 10 10\">"
      "<rect x=\"1\" y=\"2\" width=\"3\" height=\"4\"/></svg>"
      "<svg id=\"pct\" x=\"2\" y=\"3\" width=\"20\" height=\"10\">"
      "<rect x=\"10%\" y=\"20%\" width=\"30%\" height=\"40%\"/></svg>"
      "<svg id=\"empty\" x=\"2\" y=\"3\"><rect width=\"0\" height=\"4\"/></svg>"
      "<svg id=\"nested\" x=\"2\" y=\"3\" width=\"20\" height=\"10\" viewBox=\"0 0 10 10\">"
      "<svg x=\"2\" y=\"3\" width=\"10\" height=\"10\" viewBox=\"0 0 5 5\">"
      "<rect x=\"1\" y=\"1\" width=\"2\" height=\"2\"/></svg></svg>"
      "<svg id=\"loop\"><use href=\"#loop\"/></svg></defs>"
      "<g id=\"g\"><use id=\"u\" href=\"#source\" x=\"5\" y=\"7\" transform=\"translate(100,0)\"/></g></svg>'");
    auto box = [&](const char* id) {
        return S(("(function(){var b=document.getElementById('" + std::string(id) +
            "').getBBox();return [b.x,b.y,b.width,b.height].join(',')})()").c_str());
    };
    assert(box("u") == "13,12,3,4");
    assert(box("g") == "113,12,3,4");
    S("var u=document.getElementById('u');u.setAttribute('width','10');u.setAttribute('height','10')");
    assert(box("u") == "8,12,3,4");
    S("u.setAttribute('width','20');document.getElementById('source').setAttribute('preserveAspectRatio','none')");
    assert(box("u") == "9,12,6,4");
    S("u.setAttribute('width','0')");
    assert(box("u") == "8,12,3,4");
    S("u.setAttribute('href','#pct');u.setAttribute('width','20');u.setAttribute('height','10')");
    assert(box("u") == "9,12,6,4");
    S("u.setAttribute('width','0')");
    assert(box("u") == "0,0,0,0");
    S("u.setAttribute('href','#empty');u.setAttribute('width','20')");
    assert(box("u") == "0,0,0,0");
    S("u.setAttribute('href','#nested');u.removeAttribute('width');u.removeAttribute('height')");
    assert(box("u") == "16,15,4,4");
    S("u.setAttribute('href','#loop')");
    assert(box("u") == "0,0,0,0");
}
