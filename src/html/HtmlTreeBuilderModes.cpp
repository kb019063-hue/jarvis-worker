#include "litebird/html/HtmlTreeBuilder.hpp"

#include "litebird/dom/Comment.hpp"
#include "litebird/dom/Text.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <unordered_set>

namespace litebird {

namespace {

bool ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r'; }

bool is_ws_only(const std::string& text) {
    return std::all_of(text.begin(), text.end(), ws);
}

const std::unordered_set<std::string>& special_tags() {
    static const std::unordered_set<std::string> tags = {
        "address", "applet", "area", "article", "aside", "base", "basefont",
        "bgsound", "blockquote", "body", "br", "button", "caption", "center",
        "col", "colgroup", "dd", "details", "dir", "div", "dl", "dt", "embed",
        "fieldset", "figcaption", "figure", "footer", "form", "frame",
        "frameset", "h1", "h2", "h3", "h4", "h5", "h6", "head", "header",
        "hgroup", "hr", "html", "iframe", "img", "input", "keygen", "li",
        "link", "listing", "main", "marquee", "menu", "meta", "nav", "noembed",
        "noframes", "noscript", "object", "ol", "p", "param", "plaintext",
        "pre", "script", "search", "section", "select", "source", "style",
        "summary", "table", "tbody", "td", "template", "textarea", "tfoot",
        "th", "thead", "title", "tr", "track", "ul", "wbr", "xmp"};
    return tags;
}

bool is_special(const std::string& tag) { return special_tags().count(tag) > 0; }

bool is_formatting(const std::string& tag) {
    static const std::unordered_set<std::string> tags = {
        "a", "b", "big", "code", "em", "font", "i", "nobr", "s", "small",
        "strike", "strong", "tt", "u"};
    return tags.count(tag) > 0;
}

bool is_block_container(const std::string& tag) {
    static const std::unordered_set<std::string> tags = {
        "address", "article", "aside", "blockquote", "center", "details",
        "dialog", "dir", "div", "dl", "fieldset", "figcaption", "figure",
        "footer", "header", "hgroup", "main", "menu", "nav", "ol", "p",
        "search", "section", "summary", "ul"};
    return tags.count(tag) > 0;
}

bool is_heading(const std::string& tag) {
    return tag == "h1" || tag == "h2" || tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6";
}

/// Tags that break out of foreign (SVG/MathML) content back into HTML.
bool is_foreign_breakout_tag(const std::string& tag) {
    static const std::unordered_set<std::string> tags = {
        "b", "big", "blockquote", "body", "br", "center", "code", "dd", "div",
        "dl", "dt", "em", "embed", "h1", "h2", "h3", "h4", "h5", "h6", "head",
        "hr", "i", "img", "li", "listing", "menu", "meta", "nobr", "ol", "p",
        "pre", "ruby", "s", "small", "span", "strong", "strike", "sub", "sup",
        "table", "tt", "u", "ul", "var"};
    return tags.find(tag) != tags.end();
}

bool is_metadata_tag(const std::string& tag) {
    return tag == "base" || tag == "basefont" || tag == "bgsound" || tag == "link" ||
           tag == "meta" || tag == "noframes" || tag == "script" || tag == "style" ||
           tag == "template" || tag == "title";
}

} // namespace

// ---------------------------------------------------------------------------
// Initial -> BeforeHtml -> BeforeHead
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_initial(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) return;
            m_document->set_quirks_mode(QuirksMode::Quirks);
            m_mode = Mode::BeforeHtml;
            process_token(token);
            return;
        case HtmlTokenType::Comment:
            insert_comment_at(token, m_document, nullptr);
            return;
        case HtmlTokenType::Doctype: {
            Doctype doctype;
            doctype.present = true;
            doctype.name = token.name;
            doctype.public_id = token.public_id;
            doctype.system_id = token.system_id;
            m_document->set_doctype(doctype);
            m_document->set_quirks_mode(quirks_mode_for(doctype, token.force_quirks));
            m_mode = Mode::BeforeHtml;
            return;
        }
        case HtmlTokenType::EndOfFile:
            m_document->set_quirks_mode(QuirksMode::Quirks);
            m_mode = Mode::BeforeHtml;
            process_token(token);
            return;
        default:
            m_document->set_quirks_mode(QuirksMode::Quirks);
            m_mode = Mode::BeforeHtml;
            process_token(token);
            return;
    }
}

void HtmlTreeBuilder::process_before_html(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) return;
            break;
        case HtmlTokenType::Comment:
            insert_comment_at(token, m_document, nullptr);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::EndTag:
            if (token.name == "head" || token.name == "body" || token.name == "html" || token.name == "br") break;
            return;
        case HtmlTokenType::StartTag: {
            if (token.name != "html") break;
            auto html = create_element_for_token(token, ElementNamespace::HTML);
            m_document->append_child(html);
            push_element(html);
            m_mode = Mode::BeforeHead;
            return;
        }
        case HtmlTokenType::EndOfFile:
            break;
    }

    // Anything else: create a synthetic <html> and reprocess.
    HtmlToken html_token;
    html_token.type = HtmlTokenType::StartTag;
    html_token.name = "html";
    auto html = create_element_for_token(html_token, ElementNamespace::HTML);
    if (m_fragment_case) {
        // Fragment parsing already pushed a root html element.
        m_mode = Mode::InBody;
        process_token(token);
        return;
    }
    m_document->append_child(html);
    push_element(html);
    m_mode = Mode::BeforeHead;
    process_token(token);
}

void HtmlTreeBuilder::process_before_head(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) return;
            break;
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag:
            if (token.name == "html") { send_to_in_body(token); return; }
            if (token.name == "head") {
                m_head_element = insert_html_element(token);
                m_mode = Mode::InHead;
                return;
            }
            break;
        case HtmlTokenType::EndTag:
            if (token.name == "head" || token.name == "body" || token.name == "html" || token.name == "br") break;
            return;
        case HtmlTokenType::EndOfFile:
            break;
    }

    HtmlToken head_token;
    head_token.type = HtmlTokenType::StartTag;
    head_token.name = "head";
    m_head_element = insert_html_element(head_token);
    m_mode = Mode::InHead;
    process_token(token);
}

// ---------------------------------------------------------------------------
// In head
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_in_head(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) { insert_character(token.data[0]); return; }
            break;
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag: {
            const std::string& name = token.name;
            if (name == "html") { send_to_in_body(token); return; }
            if (name == "base" || name == "basefont" || name == "bgsound" || name == "link" || name == "meta") {
                insert_html_element(token);
                pop_current_node();
                acknowledge_self_closing(token);
                return;
            }
            if (name == "title") {
                switch_to_text_mode(name, ContentModel::Rcdata);
                insert_html_element(token);
                return;
            }
            if (name == "noscript") {
                if (m_scripting) {
                    insert_html_element(token);
                    switch_to_text_mode(name, ContentModel::Rawtext);
                } else {
                    insert_html_element(token);
                    m_mode = Mode::InHeadNoscript;
                }
                return;
            }
            if (name == "noframes" || name == "style") {
                insert_html_element(token);
                switch_to_text_mode(name, ContentModel::Rawtext);
                return;
            }
            if (name == "script") {
                insert_html_element(token);
                switch_to_text_mode(name, ContentModel::ScriptData);
                return;
            }
            if (name == "template") {
                insert_html_element(token);
                push_formatting_marker();
                m_frameset_ok = false;
                m_mode = Mode::InTemplate;
                m_template_modes.push_back(Mode::InTemplate);
                return;
            }
            break;
        }
        case HtmlTokenType::EndTag:
            if (token.name == "head") {
                pop_current_node();
                m_mode = Mode::AfterHead;
                return;
            }
            if (token.name == "body" || token.name == "html" || token.name == "br") break;
            if (token.name == "template") {
                if (element_index_in_stack("template") < 0) return;
                generate_implied_end_tags();
                pop_until_tag("template");
                clear_formatting_to_marker();
                if (!m_template_modes.empty()) m_template_modes.pop_back();
                reset_insertion_mode();
                return;
            }
            return;
        case HtmlTokenType::EndOfFile:
            break;
    }

    pop_current_node();  // head
    m_mode = Mode::AfterHead;
    process_token(token);
}

void HtmlTreeBuilder::process_in_head_noscript(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) { send_to_in_head(token); return; }
            break;
        case HtmlTokenType::Comment:
            send_to_in_head(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag:
            if (token.name == "html") { send_to_in_body(token); return; }
            if (token.name == "basefont" || token.name == "bgsound" || token.name == "link" ||
                token.name == "meta" || token.name == "noframes" || token.name == "style") {
                send_to_in_head(token);
                return;
            }
            if (token.name == "head" || token.name == "noscript") return;
            break;
        case HtmlTokenType::EndTag:
            if (token.name == "noscript") {
                pop_current_node();
                m_mode = Mode::InHead;
                return;
            }
            if (token.name == "br") break;
            return;
        case HtmlTokenType::EndOfFile:
            break;
    }

    pop_current_node();  // noscript
    m_mode = Mode::InHead;
    process_token(token);
}

// ---------------------------------------------------------------------------
// After head
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_after_head(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) { insert_character(token.data[0]); return; }
            break;
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag: {
            const std::string& name = token.name;
            if (name == "html") { send_to_in_body(token); return; }
            if (name == "body") {
                insert_html_element(token);
                m_frameset_ok = false;
                m_mode = Mode::InBody;
                return;
            }
            if (name == "frameset") {
                insert_html_element(token);
                m_mode = Mode::InFrameset;
                return;
            }
            if (name == "base" || name == "basefont" || name == "bgsound" || name == "link" ||
                name == "meta" || name == "noframes" || name == "script" || name == "style" ||
                name == "template" || name == "title") {
                if (m_head_element) push_element(m_head_element);
                process_in_head(token);
                if (m_head_element) {
                    const int position = open_element_position(m_head_element);
                    if (position >= 0) erase_open_element(static_cast<std::size_t>(position));
                }
                return;
            }
            if (name == "head") return;
            break;
        }
        case HtmlTokenType::EndTag:
            if (token.name == "template") { send_to_in_head(token); return; }
            if (token.name == "body" || token.name == "html" || token.name == "br") break;
            return;
        case HtmlTokenType::EndOfFile:
            break;
    }

    HtmlToken body_token;
    body_token.type = HtmlTokenType::StartTag;
    body_token.name = "body";
    insert_html_element(body_token);
    m_mode = Mode::InBody;
    process_token(token);
}

// ---------------------------------------------------------------------------
// Text mode
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_text(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            insert_character(token.data[0]);
            return;
        case HtmlTokenType::EndOfFile:
            pop_current_node();
            m_mode = m_original_mode;
            process_token(token);
            return;
        case HtmlTokenType::EndTag:
            pop_current_node();
            m_mode = m_original_mode;
            return;
        default:
            return;
    }
}

// ---------------------------------------------------------------------------
// In body
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_in_body(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character: {
            const char c = token.data[0];
            if (c == '\0') return;
            reconstruct_formatting_elements();
            insert_character(c);
            if (!ws(c)) m_frameset_ok = false;
            return;
        }
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::EndOfFile:
            if (!m_template_modes.empty()) { send_to_in_template(token); return; }
            stop_parsing();
            return;
        case HtmlTokenType::StartTag: {
            const std::string& name = token.name;

            if (name == "html") {
                if (m_open_elements.empty()) return;
                // Inside a template the tree is a separate fragment root, so a
                // stray <html> must not mutate the document element.
                if (element_index_in_stack("template") >= 0) return;
                auto html = m_open_elements.front();
                for (const auto& [key, value] : token.attributes) {
                    if (html->element_namespace() == ElementNamespace::HTML && !html->has_attribute(key)) {
                        html->set_attribute(key, value);
                    }
                }
                return;
            }

            if (is_metadata_tag(name)) { send_to_in_head(token); return; }

            if (name == "body") {
                if (m_open_elements.size() < 2) return;
                auto body = m_open_elements[1];
                if (body->tag_name() != "body") return;
                if (element_index_in_stack("template") >= 0) return;
                m_frameset_ok = false;
                for (const auto& [key, value] : token.attributes) {
                    if (!body->has_attribute(key)) body->set_attribute(key, value);
                }
                return;
            }

            if (name == "frameset") {
                if (m_open_elements.size() < 2) return;
                if (m_open_elements[1]->tag_name() != "body") return;
                if (!m_frameset_ok) return;
                auto body = m_open_elements[1];
                if (auto parent = body->parent()) parent->remove_child(body);
                resize_open_elements(1);
                insert_html_element(token);
                m_mode = Mode::InFrameset;
                return;
            }

            if (is_block_container(name) && name != "p") {
                if (has_element_in_scope("p", Scope::Button)) close_p_element();
                insert_html_element(token);
                return;
            }

            if (name == "p") {
                if (has_element_in_scope("p", Scope::Button)) close_p_element();
                insert_html_element(token);
                return;
            }

            if (is_heading(name)) {
                if (has_element_in_scope("p", Scope::Button)) close_p_element();
                if (auto current = current_node()) {
                    if (is_heading(current->tag_name())) pop_current_node();
                }
                insert_html_element(token);
                return;
            }

            if (name == "pre" || name == "listing") {
                if (has_element_in_scope("p", Scope::Button)) close_p_element();
                insert_html_element(token);
                m_skip_next_newline = true;
                m_frameset_ok = false;
                return;
            }

            if (name == "form") {
                const bool in_template = element_index_in_stack("template") >= 0;
                if (m_form_element && !in_template) return;
                if (has_element_in_scope("p", Scope::Button)) close_p_element();
                auto form = insert_html_element(token);
                if (!in_template) m_form_element = form;
                return;
            }

            if (name == "li" || name == "dd" || name == "dt") {
                m_frameset_ok = false;
                for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
                    const auto& element = m_open_elements[static_cast<std::size_t>(i)];
                    const std::string& node_name = element->tag_name();
                    if (name == "li" && node_name == "li") {
                        generate_implied_end_tags("li");
                        pop_until_tag("li");
                        break;
                    }
                    if ((name == "dd" || name == "dt") && (node_name == "dd" || node_name == "dt")) {
                        generate_implied_end_tags(node_name);
                        pop_until_tag(node_name);
                        break;
                    }
                    if (is_special(node_name) && node_name != "address" && node_name != "div" && node_name != "p") {
                        break;
                    }
                }
                if (has_element_in_scope("p", Scope::Button)) close_p_element();
                insert_html_element(token);
                return;
            }

            if (name == "plaintext") {
                if (has_element_in_scope("p", Scope::Button)) close_p_element();
                insert_html_element(token);
                if (m_tokenizer) m_tokenizer->set_content_model(ContentModel::Plaintext, name);
                return;
            }

            if (name == "button") {
                if (has_element_in_scope("button")) {
                    generate_implied_end_tags();
                    pop_until_tag("button");
                }
                reconstruct_formatting_elements();
                insert_html_element(token);
                m_frameset_ok = false;
                return;
            }

            if (name == "a") {
                if (const auto* entry = last_formatting_entry_by_tag("a")) {
                    // Keep the element, not the entry: the adoption agency
                    // erases from and inserts into the list the entry lives
                    // in, and afterwards the same slot holds another
                    // formatting element (or none). Removing "that element"
                    // through the stale entry closed whichever element had
                    // moved into the slot: <a><em><div><a> lost its <em>.
                    const std::shared_ptr<Element> open_a = entry->element;
                    HtmlToken a_token;
                    a_token.type = HtmlTokenType::EndTag;
                    a_token.name = "a";
                    adoption_agency(a_token);
                    const int index = formatting_entry_index(open_a);
                    if (index >= 0) m_formatting.erase(m_formatting.begin() + index);
                    const int position = open_element_position(open_a);
                    if (position >= 0) erase_open_element(static_cast<std::size_t>(position));
                }
                reconstruct_formatting_elements();
                auto element = insert_html_element(token);
                push_formatting_element(element, token);
                return;
            }

            // <nobr> has its own rule below (it runs the adoption agency when
            // another nobr is already in scope), so it must not be swallowed
            // by the generic formatting-element branch.
            if (is_formatting(name) && name != "nobr") {
                reconstruct_formatting_elements();
                auto element = insert_html_element(token);
                push_formatting_element(element, token);
                return;
            }

            if (name == "nobr") {
                reconstruct_formatting_elements();
                if (has_element_in_scope("nobr")) {
                    HtmlToken end;
                    end.type = HtmlTokenType::EndTag;
                    end.name = "nobr";
                    adoption_agency(end);
                    reconstruct_formatting_elements();
                }
                auto element = insert_html_element(token);
                push_formatting_element(element, token);
                return;
            }

            if (name == "applet" || name == "marquee" || name == "object") {
                reconstruct_formatting_elements();
                insert_html_element(token);
                push_formatting_marker();
                m_frameset_ok = false;
                return;
            }

            if (name == "table") {
                if (m_document->quirks_mode() != QuirksMode::Quirks && has_element_in_scope("p", Scope::Button)) {
                    close_p_element();
                }
                insert_html_element(token);
                m_frameset_ok = false;
                m_mode = Mode::InTable;
                return;
            }

            if (name == "area" || name == "br" || name == "embed" || name == "img" ||
                name == "keygen" || name == "wbr") {
                reconstruct_formatting_elements();
                insert_html_element(token);
                pop_current_node();
                acknowledge_self_closing(token);
                m_frameset_ok = false;
                return;
            }

            if (name == "input") {
                reconstruct_formatting_elements();
                insert_html_element(token);
                pop_current_node();
                acknowledge_self_closing(token);
                std::string type = token.attributes.empty() ? "" : "";
                for (const auto& [key, value] : token.attributes) {
                    if (key == "type") {
                        type = value;
                        std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                    }
                }
                if (type != "hidden") m_frameset_ok = false;
                return;
            }

            if (name == "param" || name == "source" || name == "track") {
                insert_html_element(token);
                pop_current_node();
                acknowledge_self_closing(token);
                return;
            }

            if (name == "hr") {
                if (has_element_in_scope("p", Scope::Button)) close_p_element();
                insert_html_element(token);
                pop_current_node();
                acknowledge_self_closing(token);
                m_frameset_ok = false;
                return;
            }

            if (name == "image") {
                HtmlToken img = token;
                img.name = "img";
                process_token(img);
                return;
            }

            if (name == "textarea") {
                insert_html_element(token);
                switch_to_text_mode(name, ContentModel::Rcdata);
                m_skip_next_newline = true;
                m_frameset_ok = false;
                return;
            }

            if (name == "xmp") {
                if (has_element_in_scope("p", Scope::Button)) close_p_element();
                reconstruct_formatting_elements();
                m_frameset_ok = false;
                insert_html_element(token);
                switch_to_text_mode(name, ContentModel::Rawtext);
                return;
            }

            if (name == "iframe") {
                m_frameset_ok = false;
                insert_html_element(token);
                switch_to_text_mode(name, ContentModel::Rawtext);
                return;
            }

            if (name == "noembed") {
                insert_html_element(token);
                switch_to_text_mode(name, ContentModel::Rawtext);
                return;
            }

            // "in body" defers <noscript> to the "in head" rules when
            // scripting is enabled, where it becomes raw text.
            if (name == "noscript" && m_scripting) {
                insert_html_element(token);
                switch_to_text_mode(name, ContentModel::Rawtext);
                m_frameset_ok = false;
                return;
            }

            if (name == "select") {
                reconstruct_formatting_elements();
                insert_html_element(token);
                m_frameset_ok = false;
                m_mode = (m_mode == Mode::InTable || m_mode == Mode::InCaption || m_mode == Mode::InTableBody ||
                          m_mode == Mode::InRow || m_mode == Mode::InCell)
                             ? Mode::InSelectInTable
                             : Mode::InSelect;
                return;
            }

            if (name == "optgroup" || name == "option") {
                if (auto current = current_node()) {
                    if (current->tag_name() == "option") pop_current_node();
                }
                reconstruct_formatting_elements();
                insert_html_element(token);
                return;
            }

            if (name == "rb" || name == "rtc") {
                if (has_element_in_scope("ruby")) generate_implied_end_tags();
                insert_html_element(token);
                return;
            }

            if (name == "rp" || name == "rt") {
                if (has_element_in_scope("ruby")) generate_implied_end_tags("rtc");
                insert_html_element(token);
                return;
            }

            if (name == "math" || name == "svg") {
                reconstruct_formatting_elements();
                const ElementNamespace ns = (name == "math") ? ElementNamespace::MathML : ElementNamespace::SVG;
                auto element = insert_foreign_element(token, ns);
                if (token.self_closing) pop_current_node();
                (void)element;
                return;
            }

            if (name == "caption" || name == "col" || name == "colgroup" || name == "frame" ||
                name == "head" || name == "tbody" || name == "td" || name == "tfoot" ||
                name == "th" || name == "thead" || name == "tr") {
                return;
            }

            // Any other start tag.
            reconstruct_formatting_elements();
            insert_html_element(token);
            return;
        }

        case HtmlTokenType::EndTag: {
            const std::string& name = token.name;

            if (name == "template") { send_to_in_head(token); return; }

            // <pre> and <listing> own their content: the end tag closes them
            // even when a <p> is still open inside.
            if (name == "pre" || name == "listing") {
                if (!has_element_in_scope(name)) return;
                generate_implied_end_tags();
                pop_until_tag(name);
                return;
            }

            if (name == "body") {
                if (!has_element_in_scope("body")) return;
                m_mode = Mode::AfterBody;
                return;
            }

            if (name == "html") {
                if (!has_element_in_scope("body")) return;
                m_mode = Mode::AfterBody;
                process_token(token);
                return;
            }

            if (is_block_container(name) && name != "p") {
                if (is_heading(name)) {
                    if (!has_element_in_scope_where({"h1", "h2", "h3", "h4", "h5", "h6"}, Scope::Default)) return;
                    generate_implied_end_tags();
                    pop_until_one_of({"h1", "h2", "h3", "h4", "h5", "h6"});
                    return;
                }
                if (!has_element_in_scope(name)) return;
                generate_implied_end_tags();
                pop_until_tag(name);
                return;
            }

            if (is_heading(name)) {
                if (!has_element_in_scope_where({"h1", "h2", "h3", "h4", "h5", "h6"}, Scope::Default)) return;
                generate_implied_end_tags();
                pop_until_one_of({"h1", "h2", "h3", "h4", "h5", "h6"});
                return;
            }

            if (name == "form") {
                const bool in_template = element_index_in_stack("template") >= 0;
                if (!in_template) {
                    auto node = m_form_element;
                    m_form_element = nullptr;
                    if (!node) return;
                    if (!has_element_in_scope("form")) return;
                    generate_implied_end_tags();
                    const int position = open_element_position(node);
                    if (position >= 0) erase_open_element(static_cast<std::size_t>(position));
                    return;
                }
                if (!has_element_in_scope("form")) return;
                generate_implied_end_tags();
                pop_until_tag("form");
                return;
            }

            if (name == "p") {
                if (!has_element_in_scope("p", Scope::Button)) {
                    HtmlToken p_token;
                    p_token.type = HtmlTokenType::StartTag;
                    p_token.name = "p";
                    insert_html_element(p_token);
                }
                close_p_element();
                return;
            }

            if (name == "li") {
                if (!has_element_in_scope("li", Scope::ListItem)) return;
                generate_implied_end_tags("li");
                pop_until_tag("li");
                return;
            }

            if (name == "dd" || name == "dt") {
                if (!has_element_in_scope(name)) return;
                generate_implied_end_tags(name);
                pop_until_tag(name);
                return;
            }

            if (is_formatting(name) && name != "nobr") {
                if (!adoption_agency(token)) {
                    // "Any other end tag"
                    for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
                        auto element = m_open_elements[static_cast<std::size_t>(i)];
                        if (element->tag_name() == name) {
                            generate_implied_end_tags(name);
                            resize_open_elements(static_cast<std::size_t>(i));
                            return;
                        }
                        if (is_special(element->tag_name())) return;
                    }
                }
                return;
            }

            if (name == "nobr") {
                adoption_agency(token);
                return;
            }

            if (name == "applet" || name == "marquee" || name == "object") {
                if (!has_element_in_scope(name)) return;
                generate_implied_end_tags();
                pop_until_tag(name);
                clear_formatting_to_marker();
                return;
            }

            if (name == "br") {
                HtmlToken br;
                br.type = HtmlTokenType::StartTag;
                br.name = "br";
                reconstruct_formatting_elements();
                insert_html_element(br);
                pop_current_node();
                m_frameset_ok = false;
                return;
            }

            // Any other end tag.
            for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
                auto element = m_open_elements[static_cast<std::size_t>(i)];
                if (element->tag_name() == name) {
                    generate_implied_end_tags(name);
                    resize_open_elements(static_cast<std::size_t>(i));
                    return;
                }
                if (is_special(element->tag_name())) return;
            }
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Tables
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_in_table(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character: {
            auto current = current_node();
            if (current && (current->tag_name() == "table" || current->tag_name() == "tbody" ||
                            current->tag_name() == "tfoot" || current->tag_name() == "thead" ||
                            current->tag_name() == "tr")) {
                m_pending_table_text.clear();
                m_pending_table_text.push_back(token.data[0]);
                m_table_text_return_mode = m_mode;
                m_mode = Mode::InTableText;
                return;
            }
            break;
        }
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag: {
            const std::string& name = token.name;
            if (name == "caption") {
                clear_stack_back_to_table_context();
                push_formatting_marker();
                insert_html_element(token);
                m_mode = Mode::InCaption;
                return;
            }
            if (name == "colgroup") {
                clear_stack_back_to_table_context();
                insert_html_element(token);
                m_mode = Mode::InColumnGroup;
                return;
            }
            if (name == "col") {
                clear_stack_back_to_table_context();
                HtmlToken colgroup;
                colgroup.type = HtmlTokenType::StartTag;
                colgroup.name = "colgroup";
                insert_html_element(colgroup);
                m_mode = Mode::InColumnGroup;
                process_token(token);
                return;
            }
            if (name == "tbody" || name == "tfoot" || name == "thead") {
                clear_stack_back_to_table_context();
                insert_html_element(token);
                m_mode = Mode::InTableBody;
                return;
            }
            if (name == "td" || name == "th" || name == "tr") {
                clear_stack_back_to_table_context();
                HtmlToken tbody;
                tbody.type = HtmlTokenType::StartTag;
                tbody.name = "tbody";
                insert_html_element(tbody);
                m_mode = Mode::InTableBody;
                process_token(token);
                return;
            }
            if (name == "table") {
                if (!has_element_in_table_scope("table")) return;
                pop_until_tag("table");
                reset_insertion_mode();
                process_token(token);
                return;
            }
            if (name == "style" || name == "script" || name == "template") {
                send_to_in_head(token);
                return;
            }
            if (name == "input") {
                std::string type;
                for (const auto& [key, value] : token.attributes) {
                    if (key == "type") {
                        type = value;
                        std::transform(type.begin(), type.end(), type.begin(), ::tolower);
                    }
                }
                if (type == "hidden") {
                    insert_html_element(token);
                    pop_current_node();
                    acknowledge_self_closing(token);
                    return;
                }
            }
            if (name == "form") {
                if (element_index_in_stack("template") >= 0 || m_form_element) return;
                m_form_element = insert_html_element(token);
                pop_current_node();
                return;
            }
            break;
        }
        case HtmlTokenType::EndTag:
            if (token.name == "table") {
                if (!has_element_in_table_scope("table")) return;
                pop_until_tag("table");
                reset_insertion_mode();
                return;
            }
            if (token.name == "body" || token.name == "caption" || token.name == "col" ||
                token.name == "colgroup" || token.name == "html" || token.name == "tbody" ||
                token.name == "td" || token.name == "tfoot" || token.name == "th" ||
                token.name == "thead" || token.name == "tr") {
                return;
            }
            if (token.name == "template") { send_to_in_head(token); return; }
            break;
        case HtmlTokenType::EndOfFile:
            send_to_in_body(token);
            return;
    }

    // Anything else: foster parent, then handle with the in-body rules.
    m_foster_parenting = true;
    send_to_in_body(token);
    m_foster_parenting = false;
}

void HtmlTreeBuilder::process_in_table_text(const HtmlToken& token) {
    if (token.type == HtmlTokenType::Character) {
        m_pending_table_text.append(token.data);
        return;
    }

    std::string pending;
    pending.swap(m_pending_table_text);
    m_mode = m_table_text_return_mode;

    if (!pending.empty()) {
        if (is_ws_only(pending)) {
            insert_text(pending);
        } else {
            m_foster_parenting = true;
            const Mode saved = m_mode;
            m_mode = Mode::InBody;
            process_characters(pending);
            m_mode = saved;
            m_foster_parenting = false;
        }
    }
    process_token(token);
}

void HtmlTreeBuilder::process_in_caption(const HtmlToken& token) {
    auto close_caption = [&]() {
        generate_implied_end_tags();
        pop_until_tag("caption");
        clear_formatting_to_marker();
        m_mode = Mode::InTable;
    };

    if (token.type == HtmlTokenType::EndTag && token.name == "caption") {
        if (!has_element_in_table_scope("caption")) return;
        close_caption();
        return;
    }
    if (token.type == HtmlTokenType::StartTag) {
        const std::string& name = token.name;
        if (name == "caption" || name == "col" || name == "colgroup" || name == "tbody" ||
            name == "td" || name == "tfoot" || name == "th" || name == "thead" || name == "tr") {
            if (!has_element_in_table_scope("caption")) return;
            close_caption();
            process_token(token);
            return;
        }
    }
    if (token.type == HtmlTokenType::EndTag && token.name == "table") {
        if (!has_element_in_table_scope("caption")) return;
        close_caption();
        process_token(token);
        return;
    }
    if (token.type == HtmlTokenType::EndTag) {
        const std::string& name = token.name;
        if (name == "body" || name == "col" || name == "colgroup" || name == "html" ||
            name == "tbody" || name == "td" || name == "tfoot" || name == "th" ||
            name == "thead" || name == "tr") {
            return;
        }
    }
    send_to_in_body(token);
}

void HtmlTreeBuilder::process_in_column_group(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) { insert_character(token.data[0]); return; }
            break;
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag:
            if (token.name == "html") { send_to_in_body(token); return; }
            if (token.name == "col") {
                insert_html_element(token);
                pop_current_node();
                acknowledge_self_closing(token);
                return;
            }
            if (token.name == "template") { send_to_in_head(token); return; }
            break;
        case HtmlTokenType::EndTag:
            if (token.name == "colgroup") {
                if (current_node() && current_node()->tag_name() == "colgroup") {
                    pop_current_node();
                    m_mode = Mode::InTable;
                }
                return;
            }
            if (token.name == "col") return;
            if (token.name == "template") { send_to_in_head(token); return; }
            break;
        case HtmlTokenType::EndOfFile:
            send_to_in_body(token);
            return;
    }

    if (current_node() && current_node()->tag_name() != "colgroup") return;
    pop_current_node();
    m_mode = Mode::InTable;
    process_token(token);
}

void HtmlTreeBuilder::process_in_table_body(const HtmlToken& token) {
    if (token.type == HtmlTokenType::StartTag) {
        const std::string& name = token.name;
        if (name == "tr") {
            clear_stack_back_to_table_body_context();
            insert_html_element(token);
            m_mode = Mode::InRow;
            return;
        }
        if (name == "th" || name == "td") {
            clear_stack_back_to_table_body_context();
            HtmlToken tr;
            tr.type = HtmlTokenType::StartTag;
            tr.name = "tr";
            insert_html_element(tr);
            m_mode = Mode::InRow;
            process_token(token);
            return;
        }
        if (name == "caption" || name == "col" || name == "colgroup" || name == "tbody" ||
            name == "tfoot" || name == "thead") {
            if (!has_element_in_table_scope("tbody") && !has_element_in_table_scope("thead") &&
                !has_element_in_table_scope("tfoot")) {
                return;
            }
            clear_stack_back_to_table_body_context();
            pop_current_node();
            m_mode = Mode::InTable;
            process_token(token);
            return;
        }
    }
    if (token.type == HtmlTokenType::EndTag) {
        const std::string& name = token.name;
        if (name == "tbody" || name == "tfoot" || name == "thead") {
            if (!has_element_in_table_scope(name)) return;
            clear_stack_back_to_table_body_context();
            pop_current_node();
            m_mode = Mode::InTable;
            return;
        }
        if (name == "table") {
            if (!has_element_in_table_scope("tbody") && !has_element_in_table_scope("thead") &&
                !has_element_in_table_scope("tfoot")) {
                return;
            }
            clear_stack_back_to_table_body_context();
            pop_current_node();
            m_mode = Mode::InTable;
            process_token(token);
            return;
        }
        if (name == "body" || name == "caption" || name == "col" || name == "colgroup" ||
            name == "html" || name == "td" || name == "th" || name == "tr") {
            return;
        }
    }
    send_to_in_table(token);
}

void HtmlTreeBuilder::process_in_row(const HtmlToken& token) {
    auto close_row = [&]() {
        clear_stack_back_to_row_context();
        pop_current_node();
        m_mode = Mode::InTableBody;
    };

    if (token.type == HtmlTokenType::StartTag) {
        const std::string& name = token.name;
        if (name == "th" || name == "td") {
            clear_stack_back_to_row_context();
            insert_html_element(token);
            m_mode = Mode::InCell;
            push_formatting_marker();
            return;
        }
        if (name == "caption" || name == "col" || name == "colgroup" || name == "tbody" ||
            name == "tfoot" || name == "thead" || name == "tr") {
            if (!has_element_in_table_scope("tr")) return;
            close_row();
            process_token(token);
            return;
        }
    }
    if (token.type == HtmlTokenType::EndTag) {
        const std::string& name = token.name;
        if (name == "tr") {
            if (!has_element_in_table_scope("tr")) return;
            close_row();
            return;
        }
        if (name == "table") {
            if (!has_element_in_table_scope("tr")) return;
            close_row();
            process_token(token);
            return;
        }
        if (name == "tbody" || name == "tfoot" || name == "thead") {
            if (!has_element_in_table_scope(name)) return;
            if (!has_element_in_table_scope("tr")) return;
            close_row();
            process_token(token);
            return;
        }
        if (name == "body" || name == "caption" || name == "col" || name == "colgroup" ||
            name == "html" || name == "td" || name == "th") {
            return;
        }
    }
    send_to_in_table(token);
}

void HtmlTreeBuilder::process_in_cell(const HtmlToken& token) {
    auto close_cell = [&]() {
        generate_implied_end_tags();
        pop_until_one_of({"td", "th"});
        clear_formatting_to_marker();
        m_mode = Mode::InRow;
    };

    if (token.type == HtmlTokenType::EndTag) {
        const std::string& name = token.name;
        if (name == "td" || name == "th") {
            if (!has_element_in_table_scope(name)) return;
            generate_implied_end_tags();
            pop_until_tag(name);
            clear_formatting_to_marker();
            m_mode = Mode::InRow;
            return;
        }
        if (name == "body" || name == "caption" || name == "col" || name == "colgroup" || name == "html") {
            return;
        }
        if (name == "table" || name == "tbody" || name == "tfoot" || name == "thead" || name == "tr") {
            if (!has_element_in_table_scope(name)) return;
            close_cell();
            process_token(token);
            return;
        }
    }
    if (token.type == HtmlTokenType::StartTag) {
        const std::string& name = token.name;
        if (name == "caption" || name == "col" || name == "colgroup" || name == "tbody" ||
            name == "td" || name == "tfoot" || name == "th" || name == "thead" || name == "tr") {
            if (!has_element_in_table_scope("td") && !has_element_in_table_scope("th")) return;
            close_cell();
            process_token(token);
            return;
        }
    }
    send_to_in_body(token);
}

// ---------------------------------------------------------------------------
// Select
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_in_select(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (token.data[0] == '\0') return;
            insert_character(token.data[0]);
            return;
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag: {
            const std::string& name = token.name;
            if (name == "html") { send_to_in_body(token); return; }
            if (name == "option") {
                if (current_node() && current_node()->tag_name() == "option") pop_current_node();
                insert_html_element(token);
                return;
            }
            if (name == "optgroup") {
                if (current_node() && current_node()->tag_name() == "option") pop_current_node();
                if (current_node() && current_node()->tag_name() == "optgroup") pop_current_node();
                insert_html_element(token);
                return;
            }
            if (name == "hr") {
                if (current_node() && current_node()->tag_name() == "option") pop_current_node();
                if (current_node() && current_node()->tag_name() == "optgroup") pop_current_node();
                insert_html_element(token);
                pop_current_node();
                acknowledge_self_closing(token);
                return;
            }
            if (name == "select") {
                if (!has_element_in_select_scope("select")) return;
                pop_until_tag("select");
                reset_insertion_mode();
                return;
            }
            if (name == "input" || name == "keygen" || name == "textarea") {
                if (!has_element_in_select_scope("select")) return;
                pop_until_tag("select");
                reset_insertion_mode();
                process_token(token);
                return;
            }
            if (name == "script" || name == "template") { send_to_in_head(token); return; }
            // Unknown start tags are no longer swallowed: the modern select
            // parsing rules keep them as ordinary elements (this is what the
            // WPT tree-construction corpus expects for <select><div>).
            send_to_in_body(token);
            return;
        }
        case HtmlTokenType::EndTag: {
            const std::string& name = token.name;
            if (name == "optgroup") {
                if (current_node() && current_node()->tag_name() == "option" &&
                    m_open_elements.size() > 1 && m_open_elements[m_open_elements.size() - 2]->tag_name() == "optgroup") {
                    pop_current_node();
                }
                if (current_node() && current_node()->tag_name() == "optgroup") pop_current_node();
                return;
            }
            if (name == "option") {
                if (current_node() && current_node()->tag_name() == "option") pop_current_node();
                return;
            }
            if (name == "select") {
                if (!has_element_in_select_scope("select")) return;
                pop_until_tag("select");
                reset_insertion_mode();
                return;
            }
            if (name == "template") { send_to_in_head(token); return; }
            send_to_in_body(token);
            return;
        }
        case HtmlTokenType::EndOfFile:
            send_to_in_body(token);
            return;
    }
}

void HtmlTreeBuilder::process_in_select_in_table(const HtmlToken& token) {
    static const std::unordered_set<std::string> table_parts = {
        "caption", "table", "tbody", "tfoot", "thead", "tr", "td", "th"};

    if ((token.type == HtmlTokenType::StartTag || token.type == HtmlTokenType::EndTag) &&
        table_parts.count(token.name)) {
        if (token.type == HtmlTokenType::EndTag && !has_element_in_table_scope(token.name)) return;
        pop_until_tag("select");
        reset_insertion_mode();
        process_token(token);
        return;
    }
    process_in_select(token);
}

// ---------------------------------------------------------------------------
// Template
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_in_template(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
        case HtmlTokenType::Comment:
        case HtmlTokenType::Doctype:
            process_in_body(token);
            return;
        case HtmlTokenType::StartTag: {
            const std::string& name = token.name;
            if (name == "base" || name == "basefont" || name == "bgsound" || name == "link" ||
                name == "meta" || name == "noframes" || name == "script" || name == "style" ||
                name == "template" || name == "title") {
                process_in_head(token);
                return;
            }
            if (name == "caption" || name == "colgroup" || name == "tbody" || name == "tfoot" || name == "thead") {
                m_template_modes.pop_back();
                m_template_modes.push_back(Mode::InTable);
                m_mode = Mode::InTable;
                process_token(token);
                return;
            }
            if (name == "col") {
                m_template_modes.pop_back();
                m_template_modes.push_back(Mode::InColumnGroup);
                m_mode = Mode::InColumnGroup;
                process_token(token);
                return;
            }
            if (name == "tr") {
                m_template_modes.pop_back();
                m_template_modes.push_back(Mode::InTableBody);
                m_mode = Mode::InTableBody;
                process_token(token);
                return;
            }
            if (name == "td" || name == "th") {
                m_template_modes.pop_back();
                m_template_modes.push_back(Mode::InRow);
                m_mode = Mode::InRow;
                process_token(token);
                return;
            }
            m_template_modes.pop_back();
            m_template_modes.push_back(Mode::InBody);
            m_mode = Mode::InBody;
            process_token(token);
            return;
        }
        case HtmlTokenType::EndTag:
            if (token.name == "template") { process_in_head(token); return; }
            return;
        case HtmlTokenType::EndOfFile:
            if (element_index_in_stack("template") < 0) {
                stop_parsing();
                return;
            }
            pop_until_tag("template");
            clear_formatting_to_marker();
            if (!m_template_modes.empty()) m_template_modes.pop_back();
            reset_insertion_mode();
            process_token(token);
            return;
    }
}

// ---------------------------------------------------------------------------
// After body / frameset
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_after_body(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) { send_to_in_body(token); return; }
            break;
        case HtmlTokenType::Comment: {
            std::shared_ptr<Node> parent =
                m_open_elements.empty() ? std::static_pointer_cast<Node>(m_document)
                                        : std::static_pointer_cast<Node>(m_open_elements.front());
            insert_comment_at(token, parent, nullptr);
            return;
        }
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag:
            if (token.name == "html") { send_to_in_body(token); return; }
            break;
        case HtmlTokenType::EndTag:
            if (token.name == "html") {
                if (m_fragment_case) return;
                m_mode = Mode::AfterAfterBody;
                return;
            }
            break;
        case HtmlTokenType::EndOfFile:
            stop_parsing();
            return;
    }
    m_mode = Mode::InBody;
    process_token(token);
}

void HtmlTreeBuilder::process_in_frameset(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) { insert_character(token.data[0]); return; }
            return;
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag:
            if (token.name == "html") { send_to_in_body(token); return; }
            if (token.name == "frameset") { insert_html_element(token); return; }
            if (token.name == "frame") {
                insert_html_element(token);
                pop_current_node();
                acknowledge_self_closing(token);
                return;
            }
            if (token.name == "noframes") { process_in_head(token); return; }
            return;
        case HtmlTokenType::EndTag:
            if (token.name == "frameset") {
                if (current_node() && current_node()->tag_name() == "html") return;
                pop_current_node();
                if (!m_fragment_case && current_node() && current_node()->tag_name() != "frameset") {
                    m_mode = Mode::AfterFrameset;
                }
                return;
            }
            return;
        case HtmlTokenType::EndOfFile:
            stop_parsing();
            return;
    }
}

void HtmlTreeBuilder::process_after_frameset(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character:
            if (ws(token.data[0])) { insert_character(token.data[0]); return; }
            return;
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag:
            if (token.name == "html") { send_to_in_body(token); return; }
            if (token.name == "noframes") { process_in_head(token); return; }
            return;
        case HtmlTokenType::EndTag:
            if (token.name == "html") { m_mode = Mode::AfterAfterFrameset; return; }
            return;
        case HtmlTokenType::EndOfFile:
            stop_parsing();
            return;
    }
}

void HtmlTreeBuilder::process_after_after_body(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Comment:
            insert_comment_at(token, m_document, nullptr);
            return;
        case HtmlTokenType::Doctype:
        case HtmlTokenType::Character:
            if (token.type == HtmlTokenType::Character && !ws(token.data[0])) break;
            send_to_in_body(token);
            return;
        case HtmlTokenType::StartTag:
            if (token.name == "html") { send_to_in_body(token); return; }
            break;
        case HtmlTokenType::EndOfFile:
            stop_parsing();
            return;
        default:
            break;
    }
    m_mode = Mode::InBody;
    process_token(token);
}

void HtmlTreeBuilder::process_after_after_frameset(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Comment:
            insert_comment_at(token, m_document, nullptr);
            return;
        case HtmlTokenType::Doctype:
        case HtmlTokenType::Character:
            if (token.type == HtmlTokenType::Character && !ws(token.data[0])) return;
            send_to_in_body(token);
            return;
        case HtmlTokenType::StartTag:
            if (token.name == "html") { send_to_in_body(token); return; }
            if (token.name == "noframes") { process_in_head(token); return; }
            return;
        case HtmlTokenType::EndOfFile:
            stop_parsing();
            return;
    }
}

// ---------------------------------------------------------------------------
// Foreign content
// ---------------------------------------------------------------------------

void HtmlTreeBuilder::process_foreign_content(const HtmlToken& token) {
    switch (token.type) {
        case HtmlTokenType::Character: {
            for (char c : token.data) {
                if (c == '\0') { insert_text("\xEF\xBF\xBD"); continue; }  // U+FFFD
                insert_character(c);
                if (!ws(c)) m_frameset_ok = false;
            }
            return;
        }
        case HtmlTokenType::Comment:
            insert_comment(token);
            return;
        case HtmlTokenType::Doctype:
            return;
        case HtmlTokenType::StartTag: {
            const std::string& name = token.name;
            bool breakout = is_foreign_breakout_tag(name);
            if (name == "font") {
                for (const auto& [key, value] : token.attributes) {
                    (void)value;
                    if (key == "color" || key == "face" || key == "size") { breakout = true; break; }
                }
            }
            if (breakout) {
                while (!m_open_elements.empty()) {
                    auto node = current_node();
                    if (node->element_namespace() == ElementNamespace::HTML) break;
                    if (is_mathml_text_integration_point(*node) || is_html_integration_point(*node)) break;
                    pop_current_node();
                }
                // Process with the insertion mode rules directly: re-entering
                // the dispatcher would just send us back to foreign content.
                process_token_in_mode(m_mode, token);
                return;
            }
            auto node = adjusted_current_node();
            const ElementNamespace ns = node ? node->element_namespace() : ElementNamespace::HTML;
            insert_foreign_element(token, ns);
            if (token.self_closing) pop_current_node();
            return;
        }
        case HtmlTokenType::EndTag: {
            // "An end tag whose tag name is 'br' or 'p'": pop out of the
            // foreign subtree, then reprocess with the HTML rules. Without
            // this, </p> inside <svg> silently disappears instead of closing
            // the SVG and starting a fresh paragraph.
            if (token.name == "br" || token.name == "p") {
                while (!m_open_elements.empty()) {
                    auto element = current_node();
                    if (element->element_namespace() == ElementNamespace::HTML) break;
                    if (is_mathml_text_integration_point(*element) || is_html_integration_point(*element)) break;
                    pop_current_node();
                }
                process_token_in_mode(m_mode, token);
                return;
            }
            auto node = current_node();
            if (node && node->tag_name() != token.name) {
                // Parse error: fall through to the search loop below.
            }
            for (int i = static_cast<int>(m_open_elements.size()) - 1; i >= 0; --i) {
                auto element = m_open_elements[static_cast<std::size_t>(i)];
                if (element->element_namespace() == ElementNamespace::HTML) {
                    process_token_in_mode(m_mode, token);
                    return;
                }
                std::string lowered = element->tag_name();
                std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
                if (lowered == token.name) {
                    resize_open_elements(static_cast<std::size_t>(i));
                    return;
                }
            }
            return;
        }
        case HtmlTokenType::EndOfFile:
            process_token_in_mode(m_mode, token);
            return;
    }
}

} // namespace litebird
