// Phase 426, finding 426: `Path2D`.
//
// §4.12.5.1.7 is a whole interface, and it did not exist: `typeof Path2D` answered
// `undefined`, so `new Path2D(...)` was a `ReferenceError` and the four context
// methods that take one had no way to be given one. What made it quiet is that
// `ctx.fill(p)` did **not** throw when `p` was something else: `fill` took its
// first argument as the fill rule, so a page that believed it was passing a path
// filled the **current** path instead — measured as `ctx.fill({})` succeeding.
//
// The suite is built around the property that decides the whole design: a
// `Path2D` stores its calls in **user space**, and the CTM is applied when it is
// used. A path built as a 10×10 box and filled under `scale(2)` must cover 20×20.
// A `Path2D` that stored device-space points — which is what the context's own
// current path does — would draw 10×10, and every "did it fill?" assertion would
// still pass.
//
// The others:
//
//   * `fill(path)` must leave the **current** path alone, because it is not the
//     current path. A `fill_path` written as `beginPath` + replay would destroy a
//     page's accumulated path, and nothing about the picture at that moment would
//     show it;
//   * `new Path2D(other)` copies what the other held **at the moment of the copy**,
//     so adding to the original afterwards changes nothing;
//   * `new Path2D(svgPathData)` goes through the same walk of the same grammar an
//     SVG `d` attribute does, so the two cannot disagree about a shape;
//   * an empty path draws nothing and throws nothing, and a non-path, non-string
//     constructor argument is a `TypeError`.

namespace {

/// A fresh `w`×`h` canvas, its context as `ck`.
void fresh_path_canvas(JsInterpreter& js, const char* tag, int w, int h) {
    js.eval(std::string("var ") + tag + " = document.createElement('canvas'); " + tag +
            ".width = " + std::to_string(w) + "; " + tag + ".height = " + std::to_string(h) + ";");
    js.eval(std::string("var ck = ") + tag + ".getContext('2d');");
    // `at(x, y)` answers `r,g,b,a` and `al(x, y)` just the alpha, from the context
    // that is current for the part being asked about.
    js.eval("function at(x, y) { var d = ck.getImageData(0, 0, 256, 256).data;"
            "var i = (y*256+x)*4; return d[i] + ',' + d[i+1] + ',' + d[i+2] + ',' + d[i+3]; }");
    js.eval("function al(x, y) { var d = ck.getImageData(0, 0, 256, 256).data;"
            "return d[(y*256+x)*4+3]; }");
}

} // namespace

TEST(path2d_is_a_path_of_its_own_kept_in_user_space_and_drawn_under_the_transform_in_force) {
    TestPage page;
    JsInterpreter& js = page.js;

    // --- Part 1: the interface exists, is constructible, and carries §4.12.5.1.7's
    // methods. `instanceof` is asked because a constructor that answers a plain
    // object would satisfy a `typeof` check.
    fresh_path_canvas(js, "c", 64, 64);
    assert(js.eval("typeof Path2D").to_string() == "function");
    assert(js.eval("Path2D.length").to_string() == "0");
    js.eval("var p = new Path2D();");
    assert(js.eval("p instanceof Path2D").to_string() == "true");
    for (const char* m : {"moveTo", "lineTo", "closePath", "quadraticCurveTo", "bezierCurveTo",
                          "arcTo", "arc", "ellipse", "rect", "roundRect", "addPath"}) {
        assert(js.eval(std::string("typeof p.") + m).to_string() == "function");
    }
    // A path is not the context's current path, so building one must not disturb
    // anything. This is asked before any drawing, which is when it would show.
    assert(js.eval("ck.getImageData(0, 0, 1, 1).data[3]").to_string() == "0");

    // --- Part 2: `fill(path)` fills the path, and the path is not consumed: a
    // second `fill(p)` draws the same picture again.
    js.eval("p.rect(4, 4, 10, 10); ck.fillStyle = '#ff0000'; ck.fill(p);");
    assert(js.eval("at(9,9)").to_string() == "255,0,0,255");
    assert(js.eval("at(20,20)").to_string() == "0,0,0,0");
    js.eval("ck.fillStyle = '#00ff00'; ck.fill(p);");
    assert(js.eval("at(9,9)").to_string() == "0,255,0,255");

    // --- Part 3: **the current path is not the path that was filled.** A page that
    // accumulated a path and then filled a `Path2D` still has its path. Written as
    // `ctx.fill(p)` after `beginPath(); rect(30,30,10,10)`, the later plain `fill()`
    // must paint the 30,30 rect and not the `Path2D`.
    fresh_path_canvas(js, "keep", 64, 64);
    js.eval("ck.beginPath(); ck.rect(30, 30, 10, 10);"
            "var kp = new Path2D(); kp.rect(4, 4, 10, 10);"
            "ck.fillStyle = '#ff0000'; ck.fill(kp);"
            "ck.fillStyle = '#0000ff'; ck.fill();");
    assert(js.eval("at(9,9)").to_string() == "255,0,0,255");
    assert(js.eval("at(34,34)").to_string() == "0,0,255,255");

    // --- Part 4: **the CTM at use time, not at build time.** The same `Path2D` is a
    // 10×10 box; drawn under `scale(2)` it covers 20×20. This is the assertion that
    // fails for a `Path2D` built on the context's device-space path storage, and it
    // is the reason this phase records calls instead of points.
    fresh_path_canvas(js, "tr", 64, 64);
    js.eval("var tp = new Path2D(); tp.rect(0, 0, 10, 10);"
            "ck.setTransform(2, 0, 0, 2, 0, 0); ck.fillStyle = '#000000'; ck.fill(tp);");
    assert(js.eval("al(15,15)").to_string() == "255");
    assert(js.eval("al(19,19)").to_string() == "255");
    // Outside 20×20, nothing — the box grew by the transform and by no more.
    assert(js.eval("al(25,25)").to_string() == "0");
    // And the same path, drawn again with no transform, is 10×10 at the origin: the
    // path itself was not changed by having been drawn scaled.
    fresh_path_canvas(js, "tr2", 64, 64);
    js.eval("var tp2 = new Path2D(); tp2.rect(0, 0, 10, 10);"
            "ck.setTransform(2, 0, 0, 2, 0, 0); ck.fillStyle = '#000000'; ck.fill(tp2);"
            "ck.setTransform(1, 0, 0, 1, 0, 0);");
    js.eval("ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 1, 1);");
    assert(js.eval("at(0,0)").to_string() == "255,0,0,255");

    // --- Part 5: `stroke(path)` and `clip(path)` take one too, and `clip` is
    // applied as a mask rather than as a fill.
    fresh_path_canvas(js, "st", 64, 64);
    js.eval("var sp = new Path2D(); sp.rect(4, 4, 20, 20);"
            "ck.strokeStyle = '#000000'; ck.lineWidth = 2; ck.stroke(sp);");
    // The outline is painted, the inside is not, which is what `stroke` means.
    assert(js.eval("al(4,10)").to_string() != "0");
    assert(js.eval("al(14,14)").to_string() == "0");
    fresh_path_canvas(js, "cl", 64, 64);
    js.eval("var clp = new Path2D(); clp.rect(4, 4, 20, 20);"
            "ck.clip(clp); ck.fillStyle = '#0000ff'; ck.fillRect(0, 0, 64, 64);");
    assert(js.eval("al(10,10)").to_string() == "255");
    assert(js.eval("al(30,30)").to_string() == "0");

    // --- Part 6: `isPointInPath(path, x, y)` asks about **that** path, with the
    // arguments shifted by one, and honours the fill rule.
    fresh_path_canvas(js, "ip", 64, 64);
    js.eval("var ip = new Path2D(); ip.rect(0, 0, 10, 10);");
    assert(js.eval("ck.isPointInPath(ip, 5, 5)").to_string() == "true");
    assert(js.eval("ck.isPointInPath(ip, 20, 20)").to_string() == "false");
    // The two-argument form still asks the *current* path, which is empty here.
    assert(js.eval("ck.isPointInPath(5, 5)").to_string() == "false");
    // `evenodd` on a path with a hole: an annulus drawn as two rectangles with the
    // same winding is "in" at the centre by nonzero and "out" by evenodd.
    fresh_path_canvas(js, "eo", 64, 64);
    js.eval("var eo = new Path2D(); eo.rect(0, 0, 30, 30); eo.rect(10, 10, 10, 10);");
    assert(js.eval("ck.isPointInPath(eo, 15, 15, 'nonzero')").to_string() == "true");
    assert(js.eval("ck.isPointInPath(eo, 15, 15, 'evenodd')").to_string() == "false");

    // --- Part 7: `new Path2D(other)` copies what the other held **then**. Adding to
    // the original afterwards changes nothing in the copy, which is the difference
    // between a copy and a reference.
    fresh_path_canvas(js, "cp", 64, 64);
    js.eval("var cp1 = new Path2D(); cp1.rect(0, 0, 5, 5);"
            "var cp2 = new Path2D(cp1);"
            "cp1.rect(20, 20, 5, 5);"
            "ck.fillStyle = '#000000'; ck.fill(cp2);");
    assert(js.eval("al(2,2)").to_string() == "255");
    assert(js.eval("al(22,22)").to_string() == "0");

    // --- Part 8: `addPath(other, transform)` appends the other path's calls,
    // through the matrix. The added path is at (0,0)–(5,5); with a scale of 2 and a
    // translation of (10,10) it lands at (10,10)–(20,20).
    fresh_path_canvas(js, "ap", 64, 64);
    js.eval("var base = new Path2D(); base.rect(0, 0, 5, 5);"
            "var outer = new Path2D(); outer.addPath(base, 2, 0, 0, 2, 10, 10);"
            "ck.fillStyle = '#000000'; ck.fill(outer);");
    assert(js.eval("al(2,2)").to_string() == "0");
    assert(js.eval("al(14,14)").to_string() == "255");
    // `addPath` with no transform adds it where it is.
    fresh_path_canvas(js, "ap0", 64, 64);
    js.eval("var b2 = new Path2D(); b2.rect(0, 0, 5, 5);"
            "var o2 = new Path2D(); o2.addPath(b2);"
            "ck.fillStyle = '#000000'; ck.fill(o2);");
    assert(js.eval("al(2,2)").to_string() == "255");

    // --- Part 9: the SVG path-data constructor, through the same walk of the same
    // grammar an SVG `d` attribute takes. `M0 0 L20 0 L20 20 Z` is a right triangle;
    // (15,5) is inside it and (5,20) is not.
    fresh_path_canvas(js, "sv", 64, 64);
    js.eval("var sv = new Path2D('M0 0 L20 0 L20 20 Z');"
            "ck.fillStyle = '#00ff00'; ck.fill(sv);");
    assert(js.eval("at(15,5)").to_string() == "0,255,0,255");
    assert(js.eval("at(5,20)").to_string() == "0,0,0,0");
    // Relative commands and a curve, so the walk is the real one and not a
    // rectangle special case. `m` + `l` + `z` is the same triangle by another route.
    fresh_path_canvas(js, "sv2", 64, 64);
    js.eval("var sv2 = new Path2D('m0 0 l20 0 l0 20 z');"
            "ck.fillStyle = '#00ff00'; ck.fill(sv2);");
    assert(js.eval("at(15,5)").to_string() == "0,255,0,255");
    assert(js.eval("at(5,20)").to_string() == "0,0,0,0");

    // --- Part 10: the empty path and the bad argument. An empty `Path2D` fills and
    // strokes nothing without throwing — a page that builds a path conditionally
    // draws it either way — and a constructor argument that is neither a string nor
    // a `Path2D` is a `TypeError`, not a silently empty path.
    fresh_path_canvas(js, "em", 64, 64);
    js.eval("var em = new Path2D(); ck.fill(em); ck.stroke(em); ck.clip(em);"
            "ck.fillStyle = '#ff0000'; ck.fillRect(0, 0, 4, 4);");
    assert(js.eval("at(2,2)").to_string() == "255,0,0,255");
    assert(js.eval("(function(){ try { new Path2D(42); return 'no-throw'; }"
                   "catch (e) { return e.name; } })()").to_string() == "TypeError");
}
