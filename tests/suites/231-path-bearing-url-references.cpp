// Phase 231, finding 235: path references use the URL base's actual parts.
TEST(path_references_do_not_rewrite_the_authority_or_query) {
    using namespace litebird;
    struct Case { const char* base; const char* reference; const char* expected; };
    const Case cases[] = {
        {"http://example.test/dir/page.html", "../../up", "http://example.test/up"},
        {"https://example.test/dir/page.html?old=/a/b#fragment", "next.html", "https://example.test/dir/next.html"},
        {"https://example.test/dir/page.html", "//cdn.test:8080/a//b", "https://cdn.test:8080/a//b"},
        {"http://example.test/a//b/", "./", "http://example.test/a//b/"},
        {"https://example.test/dir/page.html", "/a/../next/", "https://example.test/next/"},
        {"https://example.test/dir/page.html", "next?x=a//b#c/../d", "https://example.test/dir/next?x=a//b#c/../d"},
        {"http://example.test", "relative", "http://example.test/relative"},
        {"file:///tmp/dir/page.html", "../new.html", "file:///tmp/new.html"},
        {"https://example.test/dir/page.html", "//bad.test:invalid/a", ""},
        {"file:///tmp/page.html", "//cdn.test:8080/file", ""},
        // Existing support for absolute opaque URLs and plain local paths is
        // intentionally not normalized by this branch.
        {"https://example.test/dir/page.html", "data:font/ttf;base64,HAAD//wMg", "data:font/ttf;base64,HAAD//wMg"},
        {"https://example.test/dir/page.html", "https://EXAMPLE.test/x//y", "https://EXAMPLE.test/x//y"},
        {"examples/sub/index.html", "../hello.html", "examples/hello.html"},
        {"examples/hello.html", "flexbox.html", "examples/flexbox.html"},
    };
    for (const auto& c : cases) {
        const auto got = ResourceLoader::resolve_url(c.base, c.reference);
        if (got != c.expected) std::cerr << "resolution: " << c.base << " + " << c.reference << " -> " << got << ", expected " << c.expected << "\n";
        assert(got == c.expected);
    }

    // A query slash must not become a directory, and a scheme-relative link
    // must go to the new authority. Both are exercised through a real click
    // and file-backed, separately mapped HTTP origins, without networking.
    const auto root = std::filesystem::temp_directory_path() / ("litebird-path-nav-" + std::to_string(getpid()));
    std::filesystem::create_directories(root / "site" / "dir");
    std::filesystem::create_directories(root / "other");
    {
        std::ofstream page(root / "site" / "dir" / "start.html");
        page << "<!doctype html><a id='relative' href='next.html?x=a//b'>relative</a> "
                "<a id='network' href='//other-resolution.test/target.html'>network</a>";
    }
    { std::ofstream page(root / "site" / "dir" / "next.html"); page << "<!doctype html><p id='relative-result'>relative destination</p>"; }
    { std::ofstream page(root / "other" / "target.html"); page << "<!doctype html><p id='network-result'>network destination</p>"; }
    ResourceLoader::set_origin_root_for_testing("http://path-resolution.test", (root / "site").string());
    ResourceLoader::set_origin_root_for_testing("http://other-resolution.test", (root / "other").string());
    const std::string start = "http://path-resolution.test/dir/start.html?old=/wrong/path";
    auto click = [](BrowserShell& shell, const char* id) {
        auto link = shell.document()->get_element_by_id(id);
        assert(link);
        const auto r = link->bounding_box();
        assert(r.width > 0 && r.height > 0);
        return shell.click_at(r.x + r.width / 2, r.y + r.height / 2);
    };
    BrowserShell relative(800, 600);
    assert(relative.navigate_to(start));
    assert(click(relative, "relative"));
    assert(relative.current_url() == "http://path-resolution.test/dir/next.html?x=a//b");
    assert(relative.document()->get_element_by_id("relative-result"));
    BrowserShell network(800, 600);
    assert(network.navigate_to(start));
    assert(click(network, "network"));
    assert(network.current_url() == "http://other-resolution.test/target.html");
    assert(network.document()->get_element_by_id("network-result"));
    ResourceLoader::clear_origin_roots_for_testing();
    std::filesystem::remove_all(root);
}
