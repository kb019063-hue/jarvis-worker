// Phase 245, finding 249: Big5's single bytes, pinned to the standard so that
// the next person does not "fix" them toward a browser.
//
// This suite exists because a task went the wrong way and had to be taken back.
// It began as an attempt to make `decode_big5` answer U+0080 for a lone 0x80,
// on the strength of `new TextDecoder('big5')` in Node. The claim was checked
// against the standard's source (`whatwg/encoding`, `encoding.bs`) and it is
// false. Big5's decoder, steps 4 to 6, reads:
//
//     If byte is an ASCII byte, then return a code point whose value is byte.
//     If byte is in the range 0x81 to 0xFE, inclusive, then set Big5 leading
//     to byte and return continue.
//     Return error.
//
// There is no fourth case: 0x80 falls through to `error`, which is U+FFFD. The
// clause the task assumed exists is in *Shift_JIS* — "If byte is an ASCII byte
// or 0x80" — and in `gb18030` — "If byte is 0x80, then return U+20AC" — and
// this engine already has both of those right. That is the whole danger: three
// neighbouring decoders look alike, one of them names 0x80, and a sweep that
// compares answers across them will read the difference as a bug in Big5.
//
// ICU is the deviant here, not the engine. Node 22 answers a lone 0x80 with
// `fatal:false`, 0xFF with `''`, and never throws under `fatal:true`; all three
// are ICU departures and the engine's answers are the standard's. The vendored
// corpus cannot settle it either way, because `big5_errors.html` contains no
// 0x80 at all — which is exactly why an earlier draft "passed" WPT while being
// wrong. So the corpus is not enough and a browser is not enough. This suite
// asserts against the standard's text, and the comments say which step.
//
// Nothing in the engine changed for this phase. What changed is that the three
// answers are now written down where CI runs.

namespace {

/// The bytes `hex` names, space-separated, two hex digits each.
std::string bytes_from_hex(const std::string& hex) {
    std::string out;
    std::size_t i = 0;
    while (i < hex.size()) {
        if (hex[i] == ' ') { ++i; continue; }
        out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
        i += 2;
    }
    return out;
}

/// `bytes` read as Big5, with U+FFFD wherever the standard says error.
std::string big5(const std::string& hex) {
    std::string out;
    decode_with_encoding(bytes_from_hex(hex), EncodingId::Big5, out, false);
    return out;
}

} // namespace

TEST(0x80_is_an_error_in_big5_and_a_character_in_the_decoders_beside_it) {
    const std::string ff = "\xEF\xBF\xBD";   // U+FFFD
    const std::string c2_80 = "\xC2\x80";    // U+0080
    const std::string e2_82_ac = "\xE2\x82\xAC";  // U+20AC

    // --- Big5: 0x80 and 0xFF are both errors, because step 6 is the only
    // case left. Step 5 covers 0x81..0xFE as a lead byte; step 4 covers ASCII.
    assert(big5("80") == ff);
    assert(big5("ff") == ff);

    // The corpus's own cases, all eight: a lead byte alone, a good pair then a
    // lead byte alone, two pairs and a lead byte, 0xFF, 奏 then 0xFF, a lead
    // byte then ASCII (which is restored and decoded on its own), a lead byte
    // then an out-of-range trail, and a pair whose pointer has no index entry.
    // These are `tests/wpt/encoding/legacy-mb-tchinese/big5/big5_errors.html`,
    // in its order, and the engine matches it byte for byte.
    assert(big5("ab") == "\xEF\xBF\xBD");
    assert(big5("ab b5 ab") == "\xE5\xA5\x8F\xEF\xBF\xBD");
    assert(big5("ab ab ab") == "\xE5\x9E\x82\xEF\xBF\xBD");
    assert(big5("ff") == "\xEF\xBF\xBD");
    assert(big5("ab b5 ff") == "\xE5\xA5\x8F\xEF\xBF\xBD");
    assert(big5("ab 31") == "\xEF\xBF\xBD" "1");
    assert(big5("ab ff") == "\xEF\xBF\xBD");
    assert(big5("c8 f2") == "\xEF\xBF\xBD");

    // --- The neighbours, which is where the confusion comes from. Shift_JIS
    // step 4 names 0x80 explicitly and gives the code point of the same value.
    // gb18030 step 5 gives the euro sign. If someone ever "unifies" these three
    // decoders' byte dispatchers, one of the two must break — and now a suite
    // says which.
    {
        std::string out;
        decode_with_encoding(bytes_from_hex("80"), EncodingId::ShiftJis, out, false);
        assert(out == c2_80);
        out.clear();
        decode_with_encoding(bytes_from_hex("80"), EncodingId::Gb18030, out, false);
        assert(out == e2_82_ac);
        // ...while Big5, on the same byte, errors. All three in one place.
        out.clear();
        decode_with_encoding(bytes_from_hex("80"), EncodingId::Big5, out, false);
        assert(out == ff);
    }

    // --- The two code points the Big5 index cannot express, which step 6's
    // table spells as a letter plus a combining mark. Pointer 1133 is 0x88 0x62.
    assert(big5("88 62") == "\xC3\x8A\xCC\x84");   // U+00CA U+0304, Ê̄
    assert(big5("88 64") == "\xC3\x8A\xCC\x8C");   // U+00CA U+030C, Ê̌
    assert(big5("88 a3") == "\xC3\xAA\xCC\x84");   // U+00EA U+0304, ê̄
    assert(big5("88 a5") == "\xC3\xAA\xCC\x8C");   // U+00EA U+030C, ê̌

    // --- `fatal` turns those errors into a throw and nothing else. U+FFFD is
    // an error the decoder found, so both single bytes throw; a character is
    // not an error, so 0x88 0x62 does not.
    {
        std::string out;
        assert(!decode_with_encoding(bytes_from_hex("80"), EncodingId::Big5, out, true));
        assert(!decode_with_encoding(bytes_from_hex("ff"), EncodingId::Big5, out, true));
        assert(decode_with_encoding(bytes_from_hex("88 62"), EncodingId::Big5, out, true));
    }

    // --- The encoder is the reason the decoder must not answer U+0080. Step 2
    // of the Big5 encoder returns a byte only for an ASCII code point, and
    // U+0080 is not one: it has no Big5 pointer either, so the encoder refuses
    // it. A decoder that produced U+0080 would make a character its own
    // encoder cannot write, and this pair is the check that they agree.
    {
        std::string encoded;
        assert(!encode_with_encoding(EncodingId::Big5, "\xC2\x80", encoded));
        assert(encoded.empty());
        // While Shift_JIS, whose decoder answered 0x80 above, encodes U+0080
        // straight back to it (its encoder's step 2 names the code point).
        encoded.clear();
        assert(encode_with_encoding(EncodingId::ShiftJis, "\xC2\x80", encoded) && encoded == "\x80");
        // And gb18030 carries U+0080 by the *four-byte* form, not by 0x80:
        // the byte 0x80 belongs to the euro sign in that encoding, so nothing
        // else may use it. This is the clearest statement of why Big5 —— which
        // has no 0x80 clause at all —— must not answer U+0080 either: no Big5
        // encoder writes that character, and no index entry carries it.
        encoded.clear();
        assert(encode_with_encoding(EncodingId::Gb18030, "\xC2\x80", encoded));
        assert(encoded == "\x81\x30\x81\x30");
        encoded.clear();
        assert(encode_with_encoding(EncodingId::Gb18030, "\xE2\x82\xAC", encoded));
    }

    // --- And the interface a page uses, because a suite that only called the
    // C++ function would pass while the binding went somewhere else.
    TestPage page;
    JsInterpreter& js = page.js;
    auto decoded = [&](const char* array, const char* options = "") {
        return js.eval(std::string("new TextDecoder('big5'") + options +
                       ").decode(new Uint8Array([" + array + "]))")
            .to_string();
    };
    assert(decoded("0x80") == ff);
    assert(decoded("0xff") == ff);
    assert(decoded("0xa4, 0x40") == "\xE4\xB8\x80");      // 一, a real character
    assert(decoded("0x88, 0x62") == "\xC3\x8A\xCC\x84");

    // `fatal: true` throws a TypeError for both, as it does for any error.
    for (const char* one : {"0x80", "0xff"}) {
        const std::string threw = js.eval(
            std::string("(function(){ try { new TextDecoder('big5',{fatal:true})"
                        ".decode(new Uint8Array([") + one + "])); return 'no throw'; }"
                        " catch (e) { return e.name; } })()")
            .to_string();
        assert(threw == "TypeError");
    }

    // A lead byte split across two chunks is one character, not two errors:
    // the decoder is stateful (step 5 sets `Big5 leading`), so this is the case
    // a one-shot test cannot reach.
    const std::string split = js.eval(
        "(function(){ var d = new TextDecoder('big5');"
        " var a = d.decode(new Uint8Array([0xab]), {stream:true});"
        " var b = d.decode(new Uint8Array([0xb5]), {stream:true});"
        " return a + '|' + b + '|' + d.decode(); })()")
        .to_string();
    assert(split == "|\xE5\xA5\x8F|");

    // Every label the standard gives this decoder reaches the same function.
    for (const char* label : {"big5", "big5-hkscs", "cn-big5", "csbig5", "x-x-big5"}) {
        const std::string name =
            std::string("new TextDecoder('") + label + "').encoding";
        assert(js.eval(name).to_string() == "big5");
    }
}
