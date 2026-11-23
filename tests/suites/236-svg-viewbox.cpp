// Phase 236, finding 240: the root SVG viewBox is a live animated rectangle.
TEST(svg_view_box_is_a_live_animated_rectangle) {
    TestPage page;
    auto& js = page.js;
    auto S = [&](const char* code) { return js.eval(code).to_string(); };
    assert(S("var svg=document.createElementNS('http://www.w3.org/2000/svg','svg');"
             "var a=svg.viewBox, r=a.baseVal, v=a.animVal;"
             "[a instanceof SVGAnimatedRect,r instanceof SVGRect,a===svg.viewBox,"
             "r===a.baseVal,v===a.animVal,r!==v,r.x,r.y,r.width,r.height].join('|')") ==
           "true|true|true|true|true|true|0|0|0|0");
    assert(S("svg.setAttribute('viewBox','1, 2, 30, 40');"
             "[r.x,r.y,r.width,r.height,v.x,v.height].join('|')") == "1|2|30|40|1|40");
    assert(S("r.x=8;r.height=-4;[svg.getAttribute('viewBox'),v.x,v.height].join('|')") == "8 2 30 -4|8|-4");
    assert(S("svg.setAttribute('viewBox','1 2 3');[r.x,r.y,r.width,r.height].join('|')") == "0|0|0|0");
    assert(S("svg.setAttribute('viewBox','1-2 3 4');[r.x,r.y,r.width,r.height].join('|')") == "1|-2|3|4");
    assert(S("svg.setAttribute('viewBox','1. 2 3 4');[r.x,r.y,r.width,r.height].join('|')") == "0|0|0|0");
    assert(S("svg.removeAttribute('viewBox');r.width=12;svg.getAttribute('viewBox')") == "0 0 12 0");
    assert(S("try { v.x=9; 'no'; } catch(e) { e.name; }") == "NoModificationAllowedError");
    assert(S("var touched=0;try {v.x={valueOf:function(){touched++;return 9;}};} catch(e) {} touched") == "1");
    assert(S("try { Object.getOwnPropertyDescriptor(SVGRect.prototype,'x').get.call({}); 'no'; } catch(e) { e.name; }") == "TypeError");
    assert(S("try { new SVGAnimatedRect(); 'no'; } catch(e) { e.name; }") == "TypeError");
    assert(S("try { new SVGRect(); 'no'; } catch(e) { e.name; }") == "TypeError");

    // A setter keeps its float, not the rounded string it wrote. An unrelated
    // attribute cannot invalidate it; even an equal viewBox write must do so.
    assert(S("r.x=1/3;var first=r.x;svg.setAttribute('data-test','x');r.x===first") == "true");
    assert(S("svg.setAttribute('viewBox',svg.getAttribute('viewBox'));r.x") == "0.33333301544189453");
    assert(S("r.y={valueOf:function(){svg.setAttribute('viewBox','4 5 6 7');return 9;}};svg.getAttribute('viewBox')") == "4 9 6 7");
    assert(S("r.x=1e300;[r.x===Infinity,svg.getAttribute('viewBox')].join('|')") == "true|Infinity 9 6 7");

    // Retaining either view keeps its detached element and wrapper alive.
    assert(S("var kept=(function(){var n=document.createElementNS('http://www.w3.org/2000/svg','svg');"
             "n.setAttribute('viewBox','2 3 4 5');return n.viewBox.baseVal;})();"
             "svg=null;a=null;r=null;v=null;'ok'") == "ok");
    litebird::Heap::default_heap().collect_garbage();
    assert(S("[kept.x,kept.height,(kept.width=11),kept.width].join('|')") == "2|5|11|11");
}
