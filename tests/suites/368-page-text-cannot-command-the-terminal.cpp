// Phase 368, finding 368: a page's own text reaches the terminal raw.
//
// The dump a person reads printed a title, an attribute value, a text node and
// a comment exactly as the page wrote them, so
//
//     <title>OK&#27;]0;OWNED&#7;X</title>
//
// made the terminal read `ESC ] 0 ; OWNED BEL` — an operating-system command
// that retitles the window — and printed nothing of it. The four fields below
// are the ones the human dump shows; the rule is the same for each, from two
// sides: the bytes a terminal acts on are gone from it, and the text that was
// never an instruction is still there in the same place. The other dump is
// unchanged, and the last case here is why.
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

    {
        const std::string text = dump_of("the tree dump");
        for (unsigned char c : text) {
            if (!is_instruction(c)) continue;
            std::cerr << "FAIL: the tree dump printed byte 0x" << std::hex
                      << static_cast<int>(c) << std::dec << "\n";
            assert(false);
        }
        // The instruction is gone, and the page's word is not: each field's
        // own text survives the byte that was removed from it.
        assert(text.find("OK") != std::string::npos);
        assert(text.find("OWNED") != std::string::npos);
        assert(text.find("title=\"x") != std::string::npos);
        assert(text.find("[2Jy") != std::string::npos);
        assert(text.find("t") != std::string::npos);
        assert(text.find("T") != std::string::npos);
        assert(text.find("c") != std::string::npos);
        assert(text.find("[1;1Hd") != std::string::npos);
    }

    // The other dump is not a report: it is the html5lib corpus's data format,
    // compared byte for byte with `tests/html5lib/tree-construction/`. That
    // corpus's case #0 of `plain-text-unsafe.dat` is a carriage return whose
    // expected tree is a text node of two lines, so this dump must keep it —
    // filtering it here would be filtering the format the tool reads, and
    // Phase 368's first draft did break that case by doing exactly that.
    // What a person reads is `--dump-dom`, above; what a tool compares is this,
    // and the shell, the error diagnostics and the human dumps are the places
    // that face a terminal.
    auto carriage_return = HtmlParser::parse("<!doctype html><body>FOO&#x000D;ZOO</body>");
    assert(carriage_return);
    std::ostringstream data_format;
    dump_tree_webplatform(data_format, *carriage_return);
    std::ostringstream human;
    carriage_return->dump(human);
    assert(data_format.str().find("FOO\rZOO") != std::string::npos);   // kept, as the corpus has it
    assert(human.str().find('\r') == std::string::npos);               // not printed by the human dump

    // The WPT dump is the input of `make conformance`, `make wpt` and
    // `make diff-trees`, which compare it to the corpus's file byte for byte,
    // so an ordinary page must still dump exactly as the corpus has it.
    // Comments in the corpus's form are unchanged; a text node is still
    // quoted.
    auto clean = HtmlParser::parse(
        "<!doctype html><html><head><title>T</title></head><body><p title=\"v\">t</p><!-- c --></body></html>");
    assert(clean);
    std::ostringstream wpt;
    dump_tree_webplatform(wpt, *clean);
    assert(wpt.str().find("title=\"v\"") != std::string::npos);
    assert(wpt.str().find("\"t\"") != std::string::npos);       // the text node, in quotes
    assert(wpt.str().find("<!--  c  -->") != std::string::npos); // the comment, as the corpus has it

    // And the removal changes nothing else: the same document without its
    // instructions dumps identically, once the replacement characters — the
    // one mark left where a byte was — are taken out of both.
    auto same = HtmlParser::parse(
        "<!doctype html><html><head><title>OK]0;OWNEDX</title></head>"
        "<body><p title=\"x[2Jy\">t]0;Te</p><!-- c[1;1Hd --></body></html>");
    assert(same);
    std::ostringstream with_escapes;
    doc->dump(with_escapes);
    std::ostringstream without;
    same->dump(without);
    auto drop_marks = [](std::string s) {
        const std::string mark = "\xef\xbf\xbd";
        for (auto at = s.find(mark); at != std::string::npos; at = s.find(mark)) s.erase(at, mark.size());
        return s;
    };
    assert(drop_marks(with_escapes.str()) == without.str());
}
