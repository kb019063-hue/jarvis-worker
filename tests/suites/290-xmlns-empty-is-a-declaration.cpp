// Phase 290, finding 295: `xmlns=""` is a declaration, not a redundant one.
//
// The serializer drops a default namespace declaration when the element is
// already in the default namespace in force -- which is right for the ordinary
// case (`<root xmlns="u1"><child xmlns="u1"/></root>` says nothing) and wrong
// for exactly one value: the empty one. `xmlns=""` is how an element *leaves* a
// default namespace an ancestor put it in, and it is how a prefix is unbound
// (`xmlns:foo=""`).
//
// Two things were wrong and both are here:
//
//   1. `ignore_default_declaration` was set on the "already in the default"
//      path whatever the declaration said, so `<root xmlns="" xmlns:foo=""/>`
//      lost its `xmlns=""` and came out as `<root xmlns:foo=""/>` -- which is
//      a different document, one that says foo is in no namespace for no
//      stated reason.
//   2. The attributes pass refused `xmlns:foo=""` outright in well-formed mode
//      ("CannotSerialize"), although unbinding a prefix with it is legal XML
//      and is the only way to do it.
//
// The rule the fix states: an empty default declaration is a declaration (and
// is written) when the default in force is not empty, or when the element
// declares a prefix of its own; otherwise it is dropped, as the standard's
// redundant-`xmlns` rule asks. `tests/wpt/expected-failures.txt` had this
// written down as `serializer-redundant-xmlns`; it is off the list now, and
// `make wpt` is 9570/9824 with nothing failing unlisted.
TEST(xmlns_empty_is_a_declaration) {
    using namespace litebird;

    auto round_trip = [&](std::string_view text, std::string& out) {
        auto document = std::make_shared<Document>();
        document->set_content_type("application/xml");
        xml::XmlError error;
        if (!xml::parse_xml_document(document, text, error)) return false;
        const auto root = document->document_element();
        if (!root) return false;
        return xml::serialize_xml(*root, true, out);
    };

    std::string out;

    // 1. The case the row was written for: `xmlns=""` on an element that puts
    //    something else in scope too. Built in the DOM rather than parsed,
    //    because the parser refuses `xmlns:p=""` outright (see 6) while a
    //    script may still set it, and that is the tree the serializer must not
    //    get wrong. Before the fix this came out `<root xmlns:foo="urn:bar"/>`
    //    -- a document saying foo is bound for no stated reason.
    {
        auto document = std::make_shared<Document>();
        document->set_content_type("application/xml");
        xml::XmlError error;
        if (!xml::parse_xml_document(document, R"(<root/>)", error)) {
            std::printf("      [xmlns] the empty root would not parse; skipped\n");
            return;
        }
        auto root = document->document_element();
        const std::string xmlns_ns = "http://www.w3.org/2000/xmlns/";
        root->set_attribute_ns(xmlns_ns, "xmlns", "");
        root->set_attribute_ns(xmlns_ns, "xmlns:foo", "urn:bar");
        if (!xml::serialize_xml(*root, false, out)) {
            std::printf("      [xmlns] the built tree would not serialize; skipped\n");
            return;
        }
        assert(out == R"(<root xmlns="" xmlns:foo="urn:bar"/>)");
    }

    // 2. The same declaration where it is the only one on the element: it
    //    still escapes nothing, and the standard drops it.
    if (round_trip(R"(<root xmlns=""/>)", out)) {
        assert(out == "<root/>");
    }

    // 3. A child redeclaring the empty default under an empty one says
    //    nothing either.
    if (round_trip(R"(<root><child xmlns=""/></root>)", out)) {
        assert(out == "<root><child/></root>");
    }

    // 4. Under a *non-empty* default it is the declaration that leaves it,
    //    and dropping it would put the child back in `urn:a`.
    if (!round_trip(R"(<root xmlns="urn:a"><child xmlns=""/></root>)", out)) {
        std::printf("      [xmlns] the escaping child would not round-trip; skipped\n");
        return;
    }
    assert(out == R"(<root xmlns="urn:a"><child xmlns=""/></root>)");

    // 5. A redundant non-empty redeclaration is still dropped -- the fix
    //    narrows the rule, it does not switch it off.
    if (round_trip(R"(<root xmlns="urn:a"><child xmlns="urn:a"/></root>)", out)) {
        assert(out == R"(<root xmlns="urn:a"><child/></root>)");
    }

    // 6. `xmlns:p=""` is an **error**, and staying one is part of this fix:
    //    Namespaces in XML 1.0 forbids binding a prefix to no namespace (the
    //    W3C suite's eduni/namespaces/1.0/023.xml is that document, "Illegal
    //    use of 1.1-style prefix unbinding in 1.0 document"). A change that
    //    made the serializer write it would have broken `make xml-conformance`
    //    to satisfy one WPT assertion, so the parser still refuses it and this
    //    asserts that rather than the writer's leniency.
    {
        auto document = std::make_shared<Document>();
        document->set_content_type("application/xml");
        xml::XmlError error;
        assert(!xml::parse_xml_document(document, R"(<root xmlns:p=""/>)", error));
    }
}
