// Phase 368, finding 368: a page's own text reaches the terminal raw.
//
// The dump of a document printed a title, an attribute value, a text node and a
// comment exactly as the page wrote them, so
//
//     <title>OK&#27;]0;OWNED&#7;X</title>
//
// made the terminal read `ESC ] 0 ; OWNED BEL` — an operating-system command
// that retitles the window — and printed nothing of it. The four fields below
// are the ones a tree dump shows; the rule is the same for each, from two
// sides: the bytes a terminal acts on are gone from the dump, and the text that
// was never an instruction is still there in the same place.
TEST(page_text_cannot_command_the_terminal) {
    // A byte no terminal acts on in the host's terminal, so a failure here
    // prints something diagnosable instead of moving the cursor.
    const std::string hostile =
        "<!doctype html><html><head><title>OK\x1b]0;OWNED\aX</title></head>"
        "<body><p title=\"x\x1b[2Jy\">t\x1b]0;T\ae</p><!-- c\x1b[1;1Hd --></body></html>";

    auto is_instruction = [](unsigned char c) {
        // C0, DEL, and the 8-bit CSI/OSC/DCS introductions. A newline is the
        // dump writer's own line break; no other one of these belongs in it.
        return c == 0x9b || c == 0x9d || c == 0x90 || (c < 0x20 && c != '\n') || c == 0x7f;
    };

    auto doc = HtmlParser::parse(hostile);
    assert(doc);

    auto dump_of = [&doc](const char* which) {
        std::ostringstream text;
        if (std::string(which) == "the tree dump") doc->dump(text);
        else dump_tree_webplatform(text, *doc);
        return text.str();
    };

    for (const char* which : {"the tree dump", "the WPT tree dump"}) {
        const std::string text = dump_of(which);
        for (unsigned char c : text) {
            if (!is_instruction(c)) continue;
            std::cerr << "FAIL: " << which << " printed byte 0x" << std::hex
                      << static_cast<int>(c) << std::dec << "\n";
            assert(false);
        }
        // The instruction is gone, and the page's word is not: each field's
        // own text survives the byte that was removed from it.
        assert(text.find("OK") != std::string::npos);
        assert(text.find("OWNED") != std::string::npos);
        assert(text.find("X\"") != std::string::npos || text.find("X") != std::string::npos);
        assert(text.find("title=\"x") != std::string::npos);
        assert(text.find("[2Jy") != std::string::npos);
        assert(text.find("t") != std::string::npos);
        assert(text.find("T") != std::string::npos);
        assert(text.find("e\"") != std::string::npos || text.find("e") != std::string::npos);
        assert(text.find("c") != std::string::npos);
        assert(text.find("[1;1Hd") != std::string::npos);
    }

    // The WPT dump is also the input of `make wpt` and `make diff-trees`, which
    // compare it to the corpus's file byte for byte: an ordinary page must
    // still dump exactly as the corpus has it. Comments in the corpus's form,
    // one space either side, are unchanged; a text node is still quoted.
    auto clean = HtmlParser::parse(
        "<!doctype html><html><head><title>T</title></head><body><p title=\"v\">t</p><!-- c --></body></html>");
    assert(clean);
    std::ostringstream wpt;
    dump_tree_webplatform(wpt, *clean);
    assert(wpt.str().find("title=\"v\"") != std::string::npos);
    assert(wpt.str().find("\"t\"") != std::string::npos);       // the text node, in quotes
    assert(wpt.str().find("<!--  c  -->") != std::string::npos); // the comment, as the corpus has it

    // And the removal changes nothing else: the same document without its
    // instructions dumps identically once the replacement characters — the
    // one mark left where a byte was — are taken out of both.
    auto same = HtmlParser::parse(
        "<!doctype html><html><head><title>OK]0;OWNEDX</title></head>"
        "<body><p title=\"x[2Jy\">t]0;Te</p><!-- c[1;1Hd --></body></html>");
    assert(same);
    std::ostringstream with_escapes;
    dump_tree_webplatform(with_escapes, *doc);
    std::ostringstream without;
    dump_tree_webplatform(without, *same);
    auto drop_marks = [](std::string s) {
        const std::string mark = "\xef\xbf\xbd";
        for (auto at = s.find(mark); at != std::string::npos; at = s.find(mark)) s.erase(at, mark.size());
        return s;
    };
    assert(drop_marks(with_escapes.str()) == without.str());
}
