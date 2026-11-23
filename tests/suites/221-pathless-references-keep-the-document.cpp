// Phase 221, finding 225: pathless references preserve the document.
TEST(query_and_fragment_references_keep_the_document) {
    using namespace litebird;
    struct Case { const char* base; const char* reference; const char* expected; };
    const Case cases[] = {
        {"http://snapshot.test/url-params/index.html", "?firstName=Ayse&food=rice%20pudding", "http://snapshot.test/url-params/index.html?firstName=Ayse&food=rice%20pudding"},
        {"https://host.test/dir/page.html?old=/dir#fragment?inside", "?new=/a#next", "https://host.test/dir/page.html?new=/a#next"},
        {"https://host.test/dir/page.html?old=/dir#fragment?inside", "#next?part", "https://host.test/dir/page.html?old=/dir#next?part"},
        {"https://host.test/dir/page.html?old#fragment", "?", "https://host.test/dir/page.html?"},
        {"https://host.test/dir/page.html?old#fragment", "#", "https://host.test/dir/page.html?old#"},
        {"https://host.test/dir/page.html?old#fragment", "", "https://host.test/dir/page.html?old"},
        {"http://host.test", "?x", "http://host.test/?x"},
        {"file:///tmp/page.html?old#fragment", "#next", "file:///tmp/page.html?old#next"},
        {"relative/page.html?old#fragment", "?new", "relative/page.html?new"},
        {"relative/page.html?old#fragment", "#next", "relative/page.html?old#next"},
        {"relative/page.html?old#fragment", "", "relative/page.html?old"},
    };
    for (const auto& c : cases) {
        const auto got = ResourceLoader::resolve_url(c.base, c.reference);
        if (got != c.expected) std::cerr << "resolution: " << c.base << " + " << c.reference << " -> " << got << ", expected " << c.expected << "\n";
        assert(got == c.expected);
    }

    // Reproduce the holdout's actual route: the shell hit-tests a link, then
    // navigates an origin mapped to a file. A directory is not a page, so the
    // old last-slash resolver reaches the error document instead.
    const auto root = std::filesystem::temp_directory_path() / ("litebird-query-nav-" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "url-params");
    {
        std::ofstream page(root / "url-params" / "index.html");
        page << "<!doctype html><a id='go' href='?firstName=Ayse&amp;food=rice%20pudding'>Open</a>"
                "<p id='result'></p><script>document.getElementById('result').textContent = location.search;</script>";
    }
    ResourceLoader::set_origin_root_for_testing("http://query-navigation.test", root.string());
    const std::string start = "http://query-navigation.test/url-params/index.html?old=1";
    const std::string expected = "http://query-navigation.test/url-params/index.html?firstName=Ayse&food=rice%20pudding";
    BrowserShell shell(800, 600);
    assert(shell.navigate_to(start));
    auto link = shell.document()->get_element_by_id("go");
    assert(link);
    const auto r = link->bounding_box();
    assert(r.width > 0 && r.height > 0);
    assert(shell.click_at(r.x + r.width / 2, r.y + r.height / 2));
    assert(shell.current_url() == expected);
    assert(shell.document()->get_element_by_id("result"));
    assert(shell.document()->get_element_by_id("result")->text_content() == "?firstName=Ayse&food=rice%20pudding");

    // Keyboard activation has a separate caller of resolve_url.
    BrowserShell keyboard(800, 600);
    assert(keyboard.navigate_to(start));
    assert(keyboard.activate_focused());
    assert(keyboard.current_url() == expected);
    assert(keyboard.document()->get_element_by_id("result"));
    ResourceLoader::clear_origin_roots_for_testing();
    std::filesystem::remove_all(root);
}

