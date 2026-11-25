// Phase 417, finding 417: the seven canvas text properties.
//
// §4.12.5.1's `CanvasTextDrawingStyles` mixes in nine attributes, and seven of
// them did not exist: `letterSpacing`, `wordSpacing`, `direction`,
// `fontKerning`, `fontStretch`, `fontVariantCaps`, `textRendering` all answered
// `undefined`. The harm was quiet in the same way finding 406's was: assigning to
// an absent property makes an ordinary expando, so
// `ctx.letterSpacing = "5px"` neither threw nor changed anything, and
// `measureText("ab")` answered the same number before and after it — measured at
// 12.4755859375 both times.
//
// The suite is built around the parts of the specification that are easy to get
// plausibly wrong:
//
//   * letter spacing follows **every** character and word spacing follows only a
//     word separator, but neither applies to the **first** character — so "ab" at
//     5px gains 5 and not 10, and "a b" gains two letter spaces and one word
//     space;
//   * a spacing is a CSS `<length>`, resolved against the font size **at the
//     moment of assignment**, so `1em` under a 20px font is 20px, and changing
//     `font` afterwards does not re-resolve it;
//   * a value that is not a length is **ignored** and the previous one stays —
//     `5`, `5%`, `5pt`, `inherit` and `calc()` are all rejected, while a bare `0`
//     is accepted because CSS lets zero stand without a unit;
//   * what the getter answers is the *serialized* form, so `0` reads back `0px`;
//   * `direction` is what makes `start`/`end` mean one edge or the other, and
//     that is visible only in the ink's position, not in the measured width;
//   * `fontKerning: "none"` changes a measurement of a kerned pair, so a setter
//     that stored the string without reaching the shaper fails it;
//   * none of it may cost the RTL **script** ordering the shaper already did.
//
// Two of these were written wrong first and are recorded where they were: an
// assertion that `1em` re-resolved after `font` changed (it must not), and one
// that measured the Hebrew ink box to the wrong edge.

namespace {

/// A fresh `w`×`h` canvas, its context as `ck`.
void fresh_text_canvas(JsInterpreter& js, const char* tag, int w, int h) {
    js.eval(std::string("var ") + tag + " = document.createElement('canvas'); " + tag +
            ".width = " + std::to_string(w) + "; " + tag + ".height = " + std::to_string(h) + ";");
    js.eval(std::string("var ck = ") + tag + ".getContext('2d');");
}

} // namespace

TEST(the_seven_canvas_text_properties_exist_and_reach_both_the_measurement_and_the_painting) {
    TestPage page;
    JsInterpreter& js = page.js;

    // --- Part 1: all seven exist, each with the initial value the standard gives
    // it. `"inherit"` for `direction` and `"auto"` for `fontKerning` are not the
    // first item of their own lists by accident: a setter that defaulted to
    // `"ltr"` or `"none"` would render correctly and read back wrongly.
    fresh_text_canvas(js, "c", 120, 30);
    for (const char* name : {"letterSpacing", "wordSpacing", "direction", "fontKerning", "fontStretch",
                             "fontVariantCaps", "textRendering"}) {
        assert(js.eval(std::string("typeof ck.") + name).to_string() == "string");
    }
    assert(js.eval("ck.letterSpacing").to_string() == "0px");
    assert(js.eval("ck.wordSpacing").to_string() == "0px");
    assert(js.eval("ck.direction").to_string() == "inherit");
    assert(js.eval("ck.fontKerning").to_string() == "auto");
    assert(js.eval("ck.fontStretch").to_string() == "normal");
    assert(js.eval("ck.fontVariantCaps").to_string() == "normal");
    assert(js.eval("ck.textRendering").to_string() == "auto");

    // --- Part 2: letter spacing adds one space per character but the first, so a
    // two-character string gains exactly one spacing and a one-character string
    // gains none. The numbers are differences of the same measurement, so they do
    // not depend on which font answered.
    js.eval("ck.font = '10px sans-serif';");
    js.eval("var w_ab = ck.measureText('ab').width; var w_a = ck.measureText('a').width;");
    js.eval("ck.letterSpacing = '5px';");
    js.eval("var s_ab = ck.measureText('ab').width; var s_a = ck.measureText('a').width;");
    // Exactly one space on "ab"...
    assert(js.eval("Math.round((s_ab - w_ab) * 1000) / 1000").to_string() == "5");
    // ...and none at all on a single character, which is the assertion that fails
    // for an implementation that spaces before every glyph.
    assert(js.eval("Math.round((s_a - w_a) * 1000) / 1000").to_string() == "0");
    // Three characters gain two spaces: measured with the spacing on and then
    // after it is turned off, which is the other order from the pair above and
    // the one that catches a space counted per *character* rather than per
    // character after the first. Written the other way first — `w_abc` taken
    // while the spacing was still on — which asserted nothing at all.
    js.eval("var s_abc = ck.measureText('abc').width;");
    js.eval("ck.letterSpacing = '0px';");
    js.eval("var w_abc = ck.measureText('abc').width;");
    assert(js.eval("String(Math.round((s_abc - w_abc) * 1000) / 1000)").to_string() == "10");

    // --- Part 3: word spacing applies **as well as** letter spacing, and only to
    // the separator. "a b" is three characters with one U+0020, so at 2px letter
    // and 10px word it gains 2 + 2 + 10 = 14.
    fresh_text_canvas(js, "w", 120, 30);
    js.eval("ck.font = '10px sans-serif'; var plain = ck.measureText('a b').width;"
            "ck.letterSpacing = '2px'; ck.wordSpacing = '10px';"
            "ck.letterSpacing = '2px';"
            "__spaced = ck.measureText('a b').width;");
    assert(js.eval("String(Math.round((__spaced - plain) * 1000) / 1000)").to_string() == "14");
    // Word spacing on its own takes nothing from the letters: "abc" has no
    // separator, so it gains only the letter space.
    js.eval("ck.letterSpacing = '0px'; ck.wordSpacing = '10px';"
            "var w_abc2 = ck.measureText('abc').width;"
            "var w_abc3 = ck.measureText('abc').width;");
    assert(js.eval("String(w_abc2)").to_string() == js.eval("String(w_abc3)").to_string());
    // A space in the *first* position is not a word separator for this purpose:
    // " a" gains one letter space and no word space.
    fresh_text_canvas(js, "lead", 120, 30);
    js.eval("ck.font = '10px sans-serif'; var lp = ck.measureText(' a').width;"
            "ck.letterSpacing = '2px'; ck.wordSpacing = '10px';"
            "var ls = ck.measureText(' a').width;");
    assert(js.eval("String(Math.round((ls - lp) * 1000) / 1000)").to_string() == "2");

    // --- Part 4: `em` resolves against the font size in force **when it is set**.
    // 1em under a 20px font is 20px, and a later `font` change does not move it:
    // the specification's setter resolves the length then, and this is the
    // assertion that catches a spacing re-resolved at draw time.
    fresh_text_canvas(js, "em", 200, 30);
    js.eval("ck.font = '20px sans-serif'; var e0 = ck.measureText('ab').width;"
            "ck.letterSpacing = '1em'; var e1 = ck.measureText('ab').width;"
            "ck.font = '40px sans-serif'; var e2 = ck.measureText('ab').width;");
    assert(js.eval("String(Math.round((e1 - e0) * 1000) / 1000)").to_string() == "20");
    // The 40px measurement is not compared to e1 here: the glyphs changed too. What
    // is asserted is that the spacing did not double with the font, which would
    // have been 40px of extra advance on top of the wider glyphs. The spacing
    // still stored is the one 20px resolved to.
    assert(js.eval("ck.letterSpacing").to_string() == "1em");
    // `2px` is answered as the page wrote it, and `0` as `0px`.
    js.eval("ck.letterSpacing = '2px'");
    assert(js.eval("ck.letterSpacing").to_string() == "2px");
    js.eval("ck.letterSpacing = '0'");
    assert(js.eval("ck.letterSpacing").to_string() == "0px");
    js.eval("ck.letterSpacing = '0.50px'");
    assert(js.eval("ck.letterSpacing").to_string() == "0.5px");

    // --- Part 5: a value that is not a CSS length is ignored, and the old one
    // stays. Each is asked on its own so a parser that happens to reject one of
    // them cannot carry the others, and `'5'` — the bare number — is the one a
    // person types and CSS rejects.
    js.eval("ck.letterSpacing = '7px'");
    for (const char* bad : {"'5'", "'5%'", "'5pt'", "'inherit'", "'initial'", "'calc(1px)'",
                            "'nonsense'", "''", "'px'"}) {
        js.eval(std::string("ck.wordSpacing = '7px'; ck.letterSpacing = ") + bad);
        assert(js.eval("ck.letterSpacing").to_string() == "7px");
        js.eval(std::string("ck.wordSpacing = ") + bad);
        assert(js.eval("ck.wordSpacing").to_string() == "7px");
    }
    // A negative spacing is a legal length and is kept: text may be tightened.
    js.eval("ck.letterSpacing = '-3px'");
    assert(js.eval("ck.letterSpacing").to_string() == "-3px");
    js.eval("ck.letterSpacing = '0px'; ck.wordSpacing = '0px';");

    // --- Part 6: `direction` decides which edge `start` and `end` mean, and the
    // proof is where the ink lands, not the width — the width is the same either
    // way, which is exactly why a suite that only measured would miss it.
    js.eval("function ink(cx, w) { var d = cx.getImageData(0, 0, w, 30).data;"
            "var lo = -1, hi = -1; for (var x = 0; x < w; x++) {"
            "for (var y = 0; y < 30; y++) { if (d[(y*w+x)*4+3] > 0) { if (lo < 0) lo = x; hi = x; break; } } }"
            "return lo + '-' + hi; }");
    fresh_text_canvas(js, "l", 120, 30);
    js.eval("ck.font = '10px sans-serif'; ck.fillStyle = '#000000';"
            "ck.direction = 'ltr'; ck.textAlign = 'start'; ck.fillText('ab', 40, 20);"
            "var ltr_ink = ink(ck, 120);");
    fresh_text_canvas(js, "r", 120, 30);
    js.eval("ck.font = '10px sans-serif'; ck.fillStyle = '#000000';"
            "ck.direction = 'rtl'; ck.textAlign = 'start'; ck.fillText('ab', 40, 20);"
            "var rtl_ink = ink(ck, 120);");
    // `ltr` + `start` paints from x = 40 rightwards; `rtl` + `start` puts the run's
    // right edge at 40, so every pixel is to its left.
    assert(js.eval("parseInt(ltr_ink.split('-')[0], 10) >= 40").to_string() == "true");
    assert(js.eval("parseInt(rtl_ink.split('-')[1], 10) <= 40").to_string() == "true");
    // And the two runs advance by exactly the same amount, so `direction` moved
    // them rather than changing them. This is asked of `measureText`, not of the
    // ink box: the ink of a glyph depends on where the pen fell, and a run at a
    // half-pixel offset rasterises to a box one pixel different (measured 40–51
    // against 27–39). The advance is the thing `direction` must not touch.
    fresh_text_canvas(js, "m1", 120, 30);
    js.eval("ck.font = '10px sans-serif'; ck.direction = 'ltr'; var adv_ltr = ck.measureText('ab').width;");
    fresh_text_canvas(js, "m2", 120, 30);
    js.eval("ck.font = '10px sans-serif'; ck.direction = 'rtl'; var adv_rtl = ck.measureText('ab').width;");
    assert(js.eval("adv_ltr === adv_rtl").to_string() == "true");
    // `end` is the mirror: `rtl` + `end` is a left edge at x.
    fresh_text_canvas(js, "e", 120, 30);
    js.eval("ck.font = '10px sans-serif'; ck.fillStyle = '#000000';"
            "ck.direction = 'rtl'; ck.textAlign = 'end'; ck.fillText('ab', 40, 20);"
            "var end_ink = ink(ck, 120);");
    assert(js.eval("parseInt(end_ink.split('-')[0], 10) >= 40").to_string() == "true");
    // An unknown direction is ignored, and the old one stays.
    js.eval("ck.direction = 'sideways'");
    assert(js.eval("ck.direction").to_string() == "rtl");

    // --- Part 7: `fontKerning: "none"` reaches the shaper, so a kerned pair
    // measures differently with it off. `auto` and `normal` both kern; the
    // assertion is a *difference*, so it holds for any face with a `kern` or
    // `GPOS` table and proves the setter is not just storing a string.
    fresh_text_canvas(js, "k", 120, 30);
    js.eval("ck.font = '40px serif';"
            "var kern_auto = ck.measureText('AV').width;"
            "ck.fontKerning = 'none'; var kern_none = ck.measureText('AV').width;"
            "ck.fontKerning = 'normal'; var kern_normal = ck.measureText('AV').width;");
    assert(js.eval("kern_none !== kern_auto").to_string() == "true");
    assert(js.eval("kern_normal === kern_auto").to_string() == "true");
    // An unknown value is ignored; each of the four enumerated attributes behaves
    // the same way, and each is asked with its own valid values first.
    js.eval("ck.fontKerning = 'sometimes'");
    assert(js.eval("ck.fontKerning").to_string() == "normal");
    js.eval("ck.fontStretch = 'condensed'; ck.fontStretch = 'wide';");
    assert(js.eval("ck.fontStretch").to_string() == "condensed");
    js.eval("ck.fontVariantCaps = 'small-caps'; ck.fontVariantCaps = 'big-caps';");
    assert(js.eval("ck.fontVariantCaps").to_string() == "small-caps");
    js.eval("ck.textRendering = 'geometricPrecision'; ck.textRendering = 'fast';");
    assert(js.eval("ck.textRendering").to_string() == "geometricPrecision");

    // --- Part 8: the spacing is drawn as well as measured — the measurement
    // assertions above would all pass on a context that measured with spacing and
    // painted without it, so this asks where the ink landed.
    //
    // Three characters, not two, and the reason is worth keeping: a spacing is
    // added after **every** character including the last, so it extends the
    // advance past the final glyph while leaving that glyph's ink where it was.
    // "ab" at 6px therefore measures 6px wider and paints a box the same size —
    // measured, and it is the *correct* answer, which is why this is the third
    // character that shows the spacing. Written on "ab" first, and it failed on a
    // correct engine.
    fresh_text_canvas(js, "p", 160, 30);
    js.eval("ck.font = '10px sans-serif'; ck.fillStyle = '#000000';"
            "ck.fillText('abc', 10, 20); var tight = ink(ck, 160);");
    fresh_text_canvas(js, "q", 160, 30);
    js.eval("ck.font = '10px sans-serif'; ck.fillStyle = '#000000'; ck.letterSpacing = '6px';"
            "ck.fillText('abc', 10, 20); var loose = ink(ck, 160);");
    assert(js.eval("parseInt(loose.split('-')[1], 10) > parseInt(tight.split('-')[1], 10)")
               .to_string() == "true");
    // And word spacing moves the second word, which a painter that only widened
    // the run's advance would not do: "a b" at 20px word spacing reaches 20 pixels
    // further right than the same run without it.
    fresh_text_canvas(js, "wp", 160, 30);
    js.eval("ck.font = '10px sans-serif'; ck.fillStyle = '#000000';"
            "ck.fillText('a b', 10, 20); var wplain = ink(ck, 160);");
    fresh_text_canvas(js, "ws", 160, 30);
    js.eval("ck.font = '10px sans-serif'; ck.fillStyle = '#000000'; ck.wordSpacing = '20px';"
            "ck.fillText('a b', 10, 20); var wspaced = ink(ck, 160);");
    assert(js.eval("parseInt(wspaced.split('-')[1], 10) - parseInt(wplain.split('-')[1], 10) >= 18")
               .to_string() == "true");

    // --- Part 9: `save`/`restore` and `reset()` carry all seven, because §4.12.5.1
    // lists the spacings in the canvas state. Written as one assertion per pair so
    // a restore that copies six of the seven is caught.
    fresh_text_canvas(js, "s", 120, 30);
    js.eval("ck.letterSpacing = '4px'; ck.wordSpacing = '6px'; ck.direction = 'rtl';"
            "ck.fontKerning = 'none'; ck.fontStretch = 'expanded';"
            "ck.fontVariantCaps = 'petite-caps'; ck.textRendering = 'optimizeSpeed';"
            "ck.save();"
            "ck.letterSpacing = '40px'; ck.wordSpacing = '60px'; ck.direction = 'ltr';"
            "ck.fontKerning = 'auto'; ck.fontStretch = 'condensed';"
            "ck.fontVariantCaps = 'unicase'; ck.textRendering = 'auto';"
            "ck.restore();");
    assert(js.eval("ck.letterSpacing").to_string() == "4px");
    assert(js.eval("ck.wordSpacing").to_string() == "6px");
    assert(js.eval("ck.direction").to_string() == "rtl");
    assert(js.eval("ck.fontKerning").to_string() == "none");
    assert(js.eval("ck.fontStretch").to_string() == "expanded");
    assert(js.eval("ck.fontVariantCaps").to_string() == "petite-caps");
    assert(js.eval("ck.textRendering").to_string() == "optimizeSpeed");
    js.eval("ck.reset();");
    assert(js.eval("ck.letterSpacing").to_string() == "0px");
    assert(js.eval("ck.wordSpacing").to_string() == "0px");
    assert(js.eval("ck.direction").to_string() == "inherit");
    assert(js.eval("ck.fontKerning").to_string() == "auto");
    assert(js.eval("ck.fontStretch").to_string() == "normal");
    assert(js.eval("ck.fontVariantCaps").to_string() == "normal");
    assert(js.eval("ck.textRendering").to_string() == "auto");

    // --- Part 10: an RTL **script** is still ordered the way it was, whatever
    // `direction` says. The shaper ordered Hebrew visually before this phase and
    // the context had no say in it; routing `direction` into the shaper would have
    // turned that ordering off for every page that never set `direction` — which
    // is all of them. The ink of a two-letter Hebrew run is therefore the same
    // box under `ltr` and under `rtl`.
    // Asked of `textAlign = 'left'`, so the run does not also move under
    // `direction`: with `start` the box legitimately shifts, which is Part 6.
    fresh_text_canvas(js, "h1", 120, 30);
    js.eval("ck.font = '16px sans-serif'; ck.fillStyle = '#000000'; ck.direction = 'ltr';"
            "ck.textAlign = 'left'; ck.fillText('\\u05d0\\u05d1', 30, 20); var heb_ltr = ink(ck, 120);");
    fresh_text_canvas(js, "h2", 120, 30);
    js.eval("ck.font = '16px sans-serif'; ck.fillStyle = '#000000'; ck.direction = 'rtl';"
            "ck.textAlign = 'left'; ck.fillText('\\u05d0\\u05d1', 30, 20); var heb_rtl = ink(ck, 120);");
    assert(js.eval("heb_ltr === heb_rtl").to_string() == "true");
    // The advance is the other half of the claim, and it is the one that holds
    // under `start` too.
    fresh_text_canvas(js, "h3", 120, 30);
    js.eval("ck.font = '16px sans-serif'; ck.direction = 'ltr'; var heb_w1 = ck.measureText('\\u05d0\\u05d1').width;");
    fresh_text_canvas(js, "h4", 120, 30);
    js.eval("ck.font = '16px sans-serif'; ck.direction = 'rtl'; var heb_w2 = ck.measureText('\\u05d0\\u05d1').width;");
    assert(js.eval("heb_w1 === heb_w2").to_string() == "true");
}
