// Phase 241, finding 245: a terminal cell carries a character, not a byte.
//
// Before this, every byte of a UTF-8 sequence was one cell and every byte
// outside 0x20..0x7e was replaced by '?': a page in any language but English
// could not be shown, and a CJK character pushed the rest of its row out by six
// columns because its three bytes got three cells. These suites hold the
// renderer to the rule a terminal actually follows — a cell is a grapheme
// cluster, a wide character takes two columns, and a control character is never
// written into the stream.
TEST(a_terminal_cell_is_a_character_not_a_byte) {
    using namespace litebird;

    // Read the public ANSI stream as cells: strip the colour escapes, which the
    // separate conformance check decodes properly, and look at the text.
    auto plain = [](const std::string& ansi) {
        std::string out;
        for (size_t i = 0; i < ansi.size();) {
            if (ansi[i] == '\033') {
                const size_t end = ansi.find('m', i + 2);
                if (end == std::string::npos) return out;
                i = end + 1;
            } else {
                out.push_back(ansi[i++]);
            }
        }
        return out;
    };
    auto first_row = [](const std::string& ansi) {
        const size_t nl = ansi.find('\n');
        return ansi.substr(0, nl == std::string::npos ? ansi.size() : nl);
    };
    auto render = [&](const std::string& utf8) {
        DrawTextCommand text;
        text.position = Point(0, 0);
        text.text = utf8;
        text.color = Color::black();
        DisplayList list;
        list.add(text);
        TerminalRenderer renderer(12, 1);
        return plain(first_row(renderer.to_ansi_string(list)));
    };

    // 1. Latin beyond ASCII. "café" is four characters: before Phase 241 the
    //    two bytes of é were two cells and both were '?'. The row is twelve
    //    columns wide, so eight spaces follow the four characters.
    assert(render("caf\xC3\xA9") == "caf\xC3\xA9        ");

    // 2. A wide character is one cell, and it moves the next character two
    //    columns. 日本語 is three characters: before, it was nine '?'s.
    assert(render("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") ==
           "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E      ");

    // 3. A combining mark joins the character before it instead of taking a
    //    cell of its own (UAX #29), so X is in column 1, not column 2.
    assert(render("e\xCC\x81X") == "e\xCC\x81X          ");

    // 4. A control character can be asked for with a numeric reference
    //    (`&#27;`), and the page does not get to send it to a terminal. ESC,
    //    DEL and the C1 CSI each become U+FFFD: a character, not a command.
    const std::string controls = render("A\033B\177C\xC2\x9B");
    // The replacement characters are written as one literal per byte: in
    // "\xEF\xBF\xBDB" the trailing B would be read as a hex digit of the
    // escape rather than the letter, which is the C++ mistake this test made
    // once and now spells out.
    const std::string control_expected = "A" "\xEF\xBF\xBD" "B" "\xEF\xBF\xBD" "C" "\xEF\xBF\xBD"
                                        "      ";
    assert(controls == control_expected);
    assert(controls.find('\033') == std::string::npos);
    assert(controls.find('\177') == std::string::npos);

    // 5. Malformed UTF-8 is a replacement character per byte that could not be
    //    decoded, and the walk does not lose its place or run off the end.
    assert(render("A\x80" "B\xE6\x97") ==
           "A" "\xEF\xBF\xBD" "B" "\xEF\xBF\xBD" "\xEF\xBF\xBD" "       ");

    // 6. The second column of a wide character is not written to the stream.
    //    The terminal advances two columns for the character itself; writing a
    //    space there as well would put the rest of the row one column right,
    //    which is exactly the defect this phase fixes.
    //    "日X" is two characters taking three columns of the twelve, so nine
    //    spaces follow and nothing was added between the 日 and the X.
    const std::string wide = render("\xE6\x97\xA5X");
    assert(wide == "\xE6\x97\xA5X         ");
    assert(wide.size() == 3 + 1 + 9);
}
