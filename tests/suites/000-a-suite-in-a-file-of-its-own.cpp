// The first suite that is a file of its own, and the one that holds the
// others to what tests/suites/README.md promises: a suite in this directory
// is run without a line in main(), before main()'s own list, once.
//
// It reads the list the suites keep (tests/test_suite.cpp, `ListedSuite`)
// while it is itself being run from that list.
TEST(a_suite_in_a_file_of_its_own_is_run_without_a_line_in_main) {
    const auto& list = suite_list();
    const std::string mine =
        "a_suite_in_a_file_of_its_own_is_run_without_a_line_in_main";

    // This suite is in the list once, and is marked as a file of its own.
    int found = 0;
    for (const auto& suite : list) {
        if (suite.name != mine) continue;
        ++found;
        assert(suite.own_file);
        assert(suite.runs == 0);  // counted when it returns, not before
    }
    assert(found == 1);

    // The suites of tests/test_suite.cpp are in the list too (every `TEST`
    // lists itself), in front of the files, and none of them has been run
    // yet: the files come first.
    std::size_t in_the_big_file = 0;
    bool a_file_was_seen = false;
    for (const auto& suite : list) {
        if (suite.own_file) {
            a_file_was_seen = true;
            continue;
        }
        ++in_the_big_file;
        assert(!a_file_was_seen);
        assert(suite.runs == 0);
    }
    assert(in_the_big_file >= 347);

    // A file of this directory is read inside a namespace named after it:
    // that is what lets two files each have a helper of the same name.
    const std::string here = __func__;
    assert(here == "test_" + mine);
    const std::string spelled = __PRETTY_FUNCTION__;
    assert(spelled.find("suite_000_a_suite_in_a_file_of_its_own::") !=
           std::string::npos);

    // No two suites of the list have the same name, wherever they are.
    std::set<std::string> names;
    for (const auto& suite : list) assert(names.insert(suite.name).second);
}
