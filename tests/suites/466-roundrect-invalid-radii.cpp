// Phase 466, finding 466: `roundRect` validates the radii before changing a path.
//
// HTML allows one to four non-negative radius values. Before this phase, every
// invalid form below was accepted:
//
//   roundRect(..., -1)                 negative scalar became a square corner
//   roundRect(..., {x:-1,y:2})         negative DOMPoint component became zero
//   roundRect(..., [])                 empty list became a square rectangle
//   roundRect(..., [1,2,3,4,5])        fifth value was silently discarded
//
// Direct and `Path2D` routes behaved alike, and nothing threw. This suite pins the
// `RangeError` boundary on both routes and, more importantly, proves validation
// happens before either route changes the path it owns.

TEST(round_rect_rejects_invalid_radii_before_it_changes_either_kind_of_path) {
    TestPage page;
    JsInterpreter& js = page.js;

    js.eval(
        "function caught466(fn) {"
        "  try { fn(); return 'no throw'; }"
        "  catch (e) { return e.name + ':' + (e instanceof RangeError); }"
        "}"
        "var canvas466 = document.createElement('canvas');"
        "canvas466.width = 80; canvas466.height = 50;"
        "var ctx466 = canvas466.getContext('2d');"
        "function alpha466(x, y) { return ctx466.getImageData(x, y, 1, 1).data[3]; }"
    );

    // --- Part 1: a negative scalar is a RangeError on both public routes. `-0`
    // is not negative and remains valid.
    assert(js.eval("caught466(function(){ ctx466.roundRect(0,0,20,20,-1); })").to_string() ==
           "RangeError:true");
    assert(js.eval("caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,-1); })").to_string() ==
           "RangeError:true");
    assert(js.eval("caught466(function(){ ctx466.roundRect(0,0,20,20,-0); })").to_string() ==
           "no throw");
    assert(js.eval("caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,-0); })").to_string() ==
           "no throw");

    // --- Part 2: either negative axis of a DOMPointInit is invalid. This is not a
    // geometry clamp: even if the other axis is positive, the call must not draw.
    for (const char* expression : {
        "caught466(function(){ ctx466.roundRect(0,0,20,20,{x:-1,y:2}); })",
        "caught466(function(){ ctx466.roundRect(0,0,20,20,{x:2,y:-1}); })",
        "caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,{x:-1,y:2}); })",
        "caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,{x:2,y:-1}); })",
    }) {
        assert(js.eval(expression).to_string() == "RangeError:true");
    }

    // --- Part 3: the engine's retained inner-array point spelling has the same
    // component rule, including when the bad corner is not first in the sequence.
    for (const char* expression : {
        "caught466(function(){ ctx466.roundRect(0,0,20,20,[[-1,2]]); })",
        "caught466(function(){ ctx466.roundRect(0,0,20,20,[[2,-1]]); })",
        "caught466(function(){ ctx466.roundRect(0,0,20,20,[2,[3,4],{x:5,y:-6},7]); })",
        "caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,[[-1,2]]); })",
        "caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,[[2,-1]]); })",
        "caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,[2,[3,4],{x:5,y:-6},7]); })",
    }) {
        assert(js.eval(expression).to_string() == "RangeError:true");
    }

    // --- Part 4: a sequence has one to four entries. Empty and five-entry lists
    // used to draw a square and silently discard the fifth value, respectively.
    for (const char* expression : {
        "caught466(function(){ ctx466.roundRect(0,0,20,20,[]); })",
        "caught466(function(){ ctx466.roundRect(0,0,20,20,[1,2,3,4,5]); })",
        "caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,[]); })",
        "caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,[1,2,3,4,5]); })",
    }) {
        assert(js.eval(expression).to_string() == "RangeError:true");
    }

    // The two boundary lengths stay valid on both routes.
    for (const char* expression : {
        "caught466(function(){ ctx466.roundRect(0,0,20,20,[1]); })",
        "caught466(function(){ ctx466.roundRect(0,0,20,20,[1,2,3,4]); })",
        "caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,[1]); })",
        "caught466(function(){ var p=new Path2D(); p.roundRect(0,0,20,20,[1,2,3,4]); })",
    }) {
        assert(js.eval(expression).to_string() == "no throw");
    }

    // --- Part 5: a direct invalid call leaves the context's current path exactly
    // as it was. If validation happened inside geometry after a move, or if the old
    // square fallback ran, the pixel at (32,7) would be painted by the bad box.
    js.eval(
        "ctx466.clearRect(0,0,80,50); ctx466.beginPath();"
        "ctx466.rect(2,2,8,8);"
        "var directBad466 = caught466(function(){ ctx466.roundRect(28,2,12,12,{x:-3,y:4}); });"
        "ctx466.fill();"
    );
    assert(js.eval("directBad466").to_string() == "RangeError:true");
    assert(js.eval("alpha466(5,5)").to_string() == "255");
    assert(js.eval("alpha466(32,7)").to_string() == "0");

    // Cardinality validation is equally atomic: neither the empty nor fifth-value
    // call contributes a rectangle to the current path.
    js.eval(
        "ctx466.clearRect(0,0,80,50); ctx466.beginPath(); ctx466.rect(2,2,8,8);"
        "caught466(function(){ ctx466.roundRect(28,2,12,12,[]); });"
        "caught466(function(){ ctx466.roundRect(48,2,12,12,[1,2,3,4,5]); });"
        "ctx466.fill();"
    );
    assert(js.eval("alpha466(5,5)").to_string() == "255");
    assert(js.eval("alpha466(32,7)").to_string() == "0");
    assert(js.eval("alpha466(52,7)").to_string() == "0");

    // --- Part 6: a rejected `Path2D.roundRect` records no operation. Valid calls
    // before and after it survive, proving the exception did not poison the object
    // or append a partial corner sequence.
    js.eval(
        "ctx466.clearRect(0,0,80,50);"
        "var path466 = new Path2D(); path466.rect(2,2,8,8);"
        "var pathBad466 = caught466(function(){ path466.roundRect(28,2,12,12,[2,{x:-3,y:4}]); });"
        "path466.roundRect(48,2,12,12,{x:4,y:2});"
        "ctx466.fill(path466);"
    );
    assert(js.eval("pathBad466").to_string() == "RangeError:true");
    assert(js.eval("alpha466(5,5)").to_string() == "255");
    assert(js.eval("alpha466(32,7)").to_string() == "0");
    assert(js.eval("alpha466(54,8)").to_string() == "255");

    // Empty and oversized sequences also leave a fresh Path2D empty. Filling them
    // over a cleared canvas must not paint the rectangles they used to create.
    js.eval(
        "ctx466.clearRect(0,0,80,50);"
        "var empty466 = new Path2D();"
        "caught466(function(){ empty466.roundRect(2,2,20,20,[]); });"
        "caught466(function(){ empty466.roundRect(28,2,20,20,[1,2,3,4,5]); });"
        "ctx466.fill(empty466);"
    );
    assert(js.eval("alpha466(5,5)").to_string() == "0");
    assert(js.eval("alpha466(32,7)").to_string() == "0");

    // --- Part 7: accepted elliptical geometry from Phase 454 is unchanged after
    // adding validation. Both routes still produce the same non-square corner.
    js.eval(
        "ctx466.clearRect(0,0,80,50); ctx466.beginPath();"
        "ctx466.roundRect(5,5,50,30,[{x:20,y:5},0,0,0]); ctx466.fill();"
        "var validDirect466 = alpha466(7,7);"
        "ctx466.clearRect(0,0,80,50);"
        "var validPath466 = new Path2D(); validPath466.roundRect(5,5,50,30,[{x:20,y:5},0,0,0]);"
        "ctx466.fill(validPath466); var validRecorded466 = alpha466(7,7);"
    );
    assert(js.eval("validDirect466 < 200").to_string() == "true");
    assert(js.eval("validRecorded466 === validDirect466").to_string() == "true");
}
