// Phase 276, finding 280: the codec row said there was no streaming.
TEST(the_codec_row_was_wrong_about_streaming) {
    using namespace litebird;
    // `AGENTS.md` said, for a hundred phases, and so did the header comment of
    // src/js/TextCodec.cpp:
    //
    //     `TextDecoder` has no `stream` option either: an option that is
    //     accepted and ignored would be worse than one that is absent
    //
    // It has had one since Phase 163. Nothing checked it, so nothing
    // contradicted the claim. These are the answers, and each was read from
    // Node v22.22.3 before it was written here.
    TestPage page;
    JsInterpreter& js = page.js;

    auto text = [&](const std::string& source) { return js.eval(source).to_string(); };

    // ---- the streaming that was said to be absent -------------------------
    // A three-byte character split across two chunks: the decoder holds the
    // partial bytes and answers nothing until they are complete. This is the
    // whole of the `stream` option's visible behaviour.
    assert(text(
        "(function(){ var d = new TextDecoder();"
        "  var a = d.decode(new Uint8Array([0xE2,0x82]), {stream:true});"
        "  var b = d.decode(new Uint8Array([0xAC]));"
        "  return JSON.stringify(a) + JSON.stringify(b); })()") == "\"\"\"€\"");

    // Flushing a held-back tail: the incomplete bytes become U+FFFD rather
    // than being lost or thrown.
    // JSON.stringify of U+FFFD is the character itself, not an escape -- the
    // escape was written here first and this suite failed on it, which is the
    // reason each of these was checked against Node before being written.
    assert(text(
        "(function(){ var d = new TextDecoder();"
        "  d.decode(new Uint8Array([0xE2,0x82]), {stream:true});"
        "  return JSON.stringify(d.decode()); })()") == "\"\uFFFD\"");

    // `fatal` does *not* throw mid-stream -- the bytes are not yet an error --
    // and does throw when the stream is flushed.
    assert(text(
        "(function(){ var d = new TextDecoder('utf-8', {fatal:true});"
        "  var held = d.decode(new Uint8Array([0xE2]), {stream:true});"
        "  try { d.decode(); return 'no throw'; } catch (e) { return JSON.stringify(held) + e.name; }"
        "})()") == "\"\"TypeError");

    // Three chunks, one character each: nothing is held when nothing is
    // partial, so all three arrive.
    assert(text(
        "(function(){ var d = new TextDecoder();"
        "  return d.decode(new Uint8Array([0x41]), {stream:true})"
        "    + d.decode(new Uint8Array([0x42]), {stream:true})"
        "    + d.decode(new Uint8Array([0x43])); })()") == "ABC");

    // ---- the defect the harness found on its first run --------------------
    // The Encoding Standard's table gives `decode` **length 0**: its argument
    // is optional, and `length` counts the parameters before the first
    // optional one. This answered 1, and nothing was checking.
    assert(text("new TextDecoder().decode.length") == "0");
    // The others, which were already right, held here so a change to the
    // arity of one does not pass unnoticed.
    assert(text("new TextEncoder().encode.length") == "0");
    assert(text("new TextEncoder().encodeInto.length") == "2");
    assert(text("TextDecoder.length") == "0");
    assert(text("TextEncoder.length") == "0");

    // ---- what the encoder does, since the row spoke about both ------------
    assert(text("new TextEncoder().encoding") == "utf-8");
    // A lone surrogate is U+FFFD, never a byte sequence: `encode()` answers
    // UTF-8 only.
    assert(text("Array.from(new TextEncoder().encode('\\uD800')).join(',')") == "239,191,189");
    assert(text("Array.from(new TextEncoder().encode('\\u00e9')).join(',')") == "195,169");

    // ---- and the thing the row was right about ----------------------------
    // The stream classes are genuinely absent. They sit on Streams, which the
    // engine has none of, so this asserts the gap rather than hiding it: the
    // day one appears, this suite fails and the row moves.
    assert(text("typeof TextDecoderStream") == "undefined");
    assert(text("typeof TextEncoderStream") == "undefined");
    assert(text("typeof ReadableStream") == "undefined");
}
