// Phase 433, finding 433: getBBox() was a layout box, and there is no layout.
//
// Phase 374 gave SVGGraphicsElement its getBBox() and recorded the row as done.
// The method existed; the geometry did not. It read Element::bounding_box(),
// which is the HTML *layout* border box -- and an SVG element has no layout
// box, so every call answered 0,0,0,0:
//
//   rect(x=1 y=2 w=3 h=4).getBBox()   Chromium [1,2,3,4]   was [0,0,0,0]
//   g over that rect                  Chromium [0,0,20,14]  was [0,0,0,0]
//   circle(cx=10 cy=10 r=4)           Chromium [6,6,8,8]    was [0,0,0,0]
//
// getBBox() is the box of the geometry the element *draws*, in its own user
// space (SVG 2 §8.10). Three rules follow from that, and each is asserted
// here because each is easy to get wrong:
//
//   1. The element's own transform is EXCLUDED and a child's is INCLUDED. A box
//      is in the element's own coordinates; a `g` reports where its children
//      draw, not where the `g` would be if it had a box.
//   2. Stroke is not part of it. getBBox() is the fill geometry; getStrokeBBox
//      is the separate member that would add the stroke, and it is not built.
//   3. Curve bounds are the curve's, not its control points'. `C0 100 100 100
//      100 0` reaches y=75, and Chromium says 75 -- a box built from the four
//      points says 100, which is 33% too tall.
//
// The box comes from the renderer's own geometry (build_shape_geometry and
// build_svg_path), not from a second model of where a shape is, so a page and
// the painter cannot disagree.
//
// Reference: pinned headless Chromium, through
// tools/conformance/verify_svg_bbox.py -- 26/27, the one difference being that
// Chromium hangs getBBox on every SVG element object while SVG 2's interface
// table puts it on SVGGraphicsElement only. jsdom has no getBBox at all.
TEST(svg_get_bbox_is_the_geometry_drawn) {
    TestPage page;
    auto& js = page.js;
    auto S = [&](const char* code) { return js.eval(code).to_string(); };
    // A page with a container, so percentages resolve against its viewport and
    // every element measured below is in the document (a detached element has
    // no box, in this engine or in a browser).
    S("document.body.innerHTML = "
      "'<svg id=\"s\" width=\"200\" height=\"100\"><g id=\"g\">"
      "<rect id=\"r\" x=\"1\" y=\"2\" width=\"3\" height=\"4\"/>"
      "<circle id=\"c\" cx=\"10\" cy=\"10\" r=\"4\"/>"
      "<line id=\"l\" x1=\"0\" y1=\"0\" x2=\"6\" y2=\"8\"/>"
      "<ellipse id=\"e\" cx=\"5\" cy=\"5\" rx=\"3\" ry=\"2\"/>"
      "<polygon id=\"pg\" points=\"0,0 10,0 5,10\"/>"
      "<polyline id=\"pl\" points=\"0,0 4,4 8,0\"/>"
      "<path id=\"pa\" d=\"M0 0 L10 10 L20 0\"/>"
      "</g></svg>';");
    // The box, to three places, as x,y,width,height.
    auto box = [&](const char* id) {
        return S(("(function(){var b=document.getElementById('" + std::string(id) +
                  "').getBBox();return [b.x,b.y,b.width,b.height].map(function(v){"
                  "return Math.round(v*1000)/1000;}).join(',');})()").c_str());
    };

    // --- The method answers a real box, not a layout one. ---
    assert(box("r") == "1,2,3,4");
    assert(box("c") == "6,6,8,8");
    assert(box("l") == "0,0,6,8");
    assert(box("e") == "2,3,6,4");
    assert(box("pg") == "0,0,10,10");
    assert(box("pl") == "0,0,8,4");
    assert(box("pa") == "0,0,20,10");
    // A container's box is the union of what its children draw.
    assert(box("g") == "0,0,20,14");

    // --- It is an SVGRect, and a fresh one each time. ---
    assert(S("document.getElementById('r').getBBox().constructor.name") == "SVGRect");
    assert(S("var rb=document.getElementById('r');rb.getBBox()===rb.getBBox()") == "false");

    // --- Rule 1: the element's own transform is excluded. ---
    assert(S("var r=document.getElementById('r');r.setAttribute('transform','translate(100,100)');"
             "1;") == "1");
    assert(box("r") == "1,2,3,4");
    // ...and a child's transform is included in its parent's box.
    assert(box("g") == "0,0,104,106");
    S("document.getElementById('r').removeAttribute('transform');");
    assert(box("g") == "0,0,20,14");
    // A container's own transform is excluded from its own box too.
    S("document.getElementById('g').setAttribute('transform','translate(50,50) scale(3)');");
    assert(box("g") == "0,0,20,14");
    S("document.getElementById('g').removeAttribute('transform');");
    // A rotation maps to the corners of the rotated box, not to its own.
    // A 10x10 square turned 45 degrees is sqrt(200) wide: 14.142.
    assert(S("var gg=document.createElementNS('http://www.w3.org/2000/svg','g');"
             "var n=document.createElementNS('http://www.w3.org/2000/svg','rect');"
             "n.setAttribute('width','10');n.setAttribute('height','10');"
             "n.setAttribute('transform','rotate(45)');gg.appendChild(n);"
             "document.getElementById('s').appendChild(gg);"
             "var b=gg.getBBox();var out=[b.width,b.height].map(function(v){return Math.round(v*1000)/1000;}).join(',');"
             "document.getElementById('s').removeChild(gg);out") == "14.142,14.142");

    // --- Rule 2: stroke is not part of the box. ---
    assert(S("var r2=document.getElementById('r');r2.setAttribute('stroke','black');"
             "r2.setAttribute('stroke-width','20');1") == "1");
    assert(box("r") == "1,2,3,4");
    S("var r3=document.getElementById('r');r3.removeAttribute('stroke');r3.removeAttribute('stroke-width');");

    // --- Rule 3: curve bounds, not control points. ---
    // The curve `M0 0 C0 100 100 100 100 0` peaks at y=75.
    assert(S("var p=document.getElementById('pa');p.setAttribute('d','M0 0 C0 100 100 100 100 0');"
             "var b=p.getBBox();[Math.round(b.y*100)/100,Math.round(b.height*100)/100].join(',')") == "0,75");
    // A quadratic peaks at half its control height: `Q50 100 100 0` is 50.
    assert(S("p.setAttribute('d','M0 0 Q50 100 100 0');"
             "var b=p.getBBox();Math.round(b.height*100)/100") == "50");
    S("p.setAttribute('d','M0 0 L10 10 L20 0');");

    // --- The viewport a percentage resolves against. ---
    // §8.10's lengths are user units, and a percentage of them is a share of
    // the viewport the element draws in -- the nearest <svg>'s. A `viewBox`
    // replaces that viewport: a 200-wide svg showing 0 0 20 10 is 20 user
    // units across, so 50% is 10 and not 100.
    // Without a viewBox the viewport is the svg's own 200 units, so 50% is 100;
    // a `viewBox` replaces that viewport, and the same 50% becomes 10.
    assert(S("var s=document.getElementById('s');var pr=document.getElementById('r');"
             "pr.setAttribute('width','50%');var out=pr.getBBox().width;"
             "s.setAttribute('viewBox','0 0 20 10');out+='|'+pr.getBBox().width;"
             "s.removeAttribute('viewBox');out") == "100|10");
    S("pr.setAttribute('width','3');");
    assert(box("r") == "1,2,3,4");

    // --- Units are user units. ---
    // 1cm is 96/2.54 user units in this engine, as in a browser at 96 dpi.
    assert(S("var cm=document.createElementNS('http://www.w3.org/2000/svg','rect');"
             "cm.setAttribute('x','1cm');cm.setAttribute('width','1cm');cm.setAttribute('height','1mm');"
             "document.getElementById('s').appendChild(cm);"
             "var b=cm.getBBox();var out=[b.width,b.height].map(function(v){"
             "return Math.round(v*1000)/1000;}).join(',');"
             "document.getElementById('s').removeChild(cm);out") == "37.795,3.78");

    // --- A shape that draws nothing. ---
    // An empty path has no box.
    assert(S("var ep=document.createElementNS('http://www.w3.org/2000/svg','path');"
             "document.getElementById('s').appendChild(ep);"
             "var b=ep.getBBox();var out=[b.x,b.y,b.width,b.height].join(',');"
             "document.getElementById('s').removeChild(ep);out") == "0,0,0,0");
    // A rect with one zero side still has the line's box, where it stands.
    assert(S("var zr=document.createElementNS('http://www.w3.org/2000/svg','rect');"
             "zr.setAttribute('x','5');zr.setAttribute('y','5');"
             "zr.setAttribute('width','0');zr.setAttribute('height','4');"
             "document.getElementById('s').appendChild(zr);"
             "var b=zr.getBBox();var out=[b.x,b.y,b.width,b.height].join(',');"
             "document.getElementById('s').removeChild(zr);out") == "5,5,0,4");

    // --- Nothing that is not rendered where it stands contributes. ---
    assert(S("var dg=document.createElementNS('http://www.w3.org/2000/svg','g');"
             "var defs=document.createElementNS('http://www.w3.org/2000/svg','defs');"
             "var dr=document.createElementNS('http://www.w3.org/2000/svg','rect');"
             "dr.setAttribute('x','500');dr.setAttribute('y','500');"
             "dr.setAttribute('width','10');dr.setAttribute('height','10');"
             "defs.appendChild(dr);dg.appendChild(defs);"
             "document.getElementById('s').appendChild(dg);"
             "var b=dg.getBBox();var out=[b.x,b.y,b.width,b.height].join(',');"
             "document.getElementById('s').removeChild(dg);out") == "0,0,0,0");

    // --- Nested groups: transforms accumulate outward. ---
    assert(S("var outer=document.createElementNS('http://www.w3.org/2000/svg','g');"
             "var inner=document.createElementNS('http://www.w3.org/2000/svg','g');"
             "inner.setAttribute('transform','translate(10,10)');"
             "var ir=document.createElementNS('http://www.w3.org/2000/svg','rect');"
             "ir.setAttribute('width','5');ir.setAttribute('height','5');"
             "inner.appendChild(ir);outer.appendChild(inner);"
             "document.getElementById('s').appendChild(outer);"
             "var ob=outer.getBBox(),ib=inner.getBBox();"
             "var out=[ob.x,ob.y,ob.width,ob.height,ib.x,ib.y,ib.width,ib.height].join(',');"
             "document.getElementById('s').removeChild(outer);out") == "10,10,5,5,0,0,5,5");

    // --- The interface table still decides who has the member. ---
    // SVG 2 §4.4.2 puts getBBox on SVGGraphicsElement, so a `defs` -- an
    // SVGElement, not a graphics one -- does not have it. A browser gives every
    // SVG element object the member; this engine follows the table, and
    // Phase 374's suite asserts the same split.
    assert(S("typeof document.createElementNS('http://www.w3.org/2000/svg','g').getBBox") == "function");
    assert(S("typeof document.createElementNS('http://www.w3.org/2000/svg','defs').getBBox") == "undefined");
    assert(S("typeof document.createElement('div').getBBox") == "undefined");
}
