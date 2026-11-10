#pragma once

#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/html/HtmlTokenizer.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace litebird {

/// An implementation of the WHATWG HTML tree construction algorithm
/// (insertion modes, implied end tags, active formatting elements with the
/// adoption agency algorithm, foster parenting and foreign content).
class HtmlTreeBuilder {
public:
    /// Parses a complete document. Never returns nullptr.
    std::shared_ptr<Document> parse(std::string_view html);

    /// Fragment parsing with an explicit context element
    /// (WHATWG HTML 13.4 "Parsing HTML fragments"). The returned node is a
    /// DocumentFragment holding the parsed children.
    std::shared_ptr<Node> parse_fragment(std::string_view html,
                                         const std::string& context_tag,
                                         ElementNamespace context_namespace = ElementNamespace::HTML);

    /// The scripting flag, which changes <noscript> handling.
    void set_scripting(bool enabled) { m_scripting = enabled; }

private:
    enum class Mode {
        Initial,
        BeforeHtml,
        BeforeHead,
        InHead,
        InHeadNoscript,
        AfterHead,
        InBody,
        Text,
        InTable,
        InTableText,
        InCaption,
        InColumnGroup,
        InTableBody,
        InRow,
        InCell,
        InSelect,
        InSelectInTable,
        InTemplate,
        AfterBody,
        InFrameset,
        AfterFrameset,
        AfterAfterBody,
        AfterAfterFrameset
    };

    enum class Scope {
        Default,
        ListItem,
        Button,
        Table,
        Select
    };

    // --- token dispatch -----------------------------------------------------
    void process_token(const HtmlToken& token);
    void process_token_in_mode(Mode mode, const HtmlToken& token);
    void process_characters(const std::string& data);
    void process_character(char c);

    void process_initial(const HtmlToken&);
    void process_before_html(const HtmlToken&);
    void process_before_head(const HtmlToken&);
    void process_in_head(const HtmlToken&);
    void process_in_head_noscript(const HtmlToken&);
    void process_after_head(const HtmlToken&);
    void process_in_body(const HtmlToken&);
    void process_text(const HtmlToken&);
    void process_in_table(const HtmlToken&);
    void process_in_table_text(const HtmlToken&);
    void process_in_caption(const HtmlToken&);
    void process_in_column_group(const HtmlToken&);
    void process_in_table_body(const HtmlToken&);
    void process_in_row(const HtmlToken&);
    void process_in_cell(const HtmlToken&);
    void process_in_select(const HtmlToken&);
    void process_in_select_in_table(const HtmlToken&);
    void process_in_template(const HtmlToken&);
    void process_after_body(const HtmlToken&);
    void process_in_frameset(const HtmlToken&);
    void process_after_frameset(const HtmlToken&);
    void process_after_after_body(const HtmlToken&);
    void process_after_after_frameset(const HtmlToken&);
    void process_foreign_content(const HtmlToken&);

    // Cross-mode "process the token using the rules for the X insertion
    // mode" helpers used throughout the spec.
    void send_to_in_body(const HtmlToken& token) { process_in_body(token); }
    void send_to_in_head(const HtmlToken& token) { process_in_head(token); }
    void send_to_in_table(const HtmlToken& token) { process_in_table(token); }
    void send_to_in_template(const HtmlToken& token) { process_in_template(token); }

    // --- stack of open elements / active formatting -------------------------
    std::shared_ptr<Element> current_node() const;
    std::shared_ptr<Element> adjusted_current_node() const;
    void push_element(const std::shared_ptr<Element>&);
    void pop_current_node();
    void resize_open_elements(std::size_t size);
    void erase_open_element(std::size_t index);
    void insert_open_element(std::size_t index, const std::shared_ptr<Element>& element);
    void replace_open_element(std::size_t index, const std::shared_ptr<Element>& element);
    void index_open_element(std::size_t index, const std::shared_ptr<Element>& element);
    void rebuild_open_element_indexes();
    void pop_until_tag(const std::string& tag);
    void pop_until_one_of(const std::vector<std::string>& tags);
    bool has_element_in_scope(const std::string& tag, Scope scope = Scope::Default) const;
    bool has_element_in_scope_where(const std::vector<std::string>& tags, Scope scope) const;
    bool has_element_in_table_scope(const std::string& tag) const;
    bool has_element_in_select_scope(const std::string& tag) const;
    int element_index_in_stack(const std::string& tag) const;
    void generate_implied_end_tags(const std::string& except = std::string());
    void generate_all_implied_end_tags();
    void clear_stack_back_to_table_context();
    void clear_stack_back_to_table_body_context();
    void clear_stack_back_to_row_context();

    // --- insertion ----------------------------------------------------------
    struct InsertionPlace {
        std::shared_ptr<Node> parent;
        std::shared_ptr<Node> before;  // null = append
    };
    /// Spec "appropriate place for inserting a node", with an optional
    /// override target (used by the adoption agency algorithm) and an
    /// explicit foster-parenting switch.
    InsertionPlace appropriate_place_for_inserting_node(
        std::shared_ptr<Element> override_target = nullptr,
        bool force_foster = false);
    std::shared_ptr<Element> create_element_for_token(const HtmlToken& token, ElementNamespace ns);
    std::shared_ptr<Element> insert_html_element(const HtmlToken& token);
    std::shared_ptr<Element> insert_foreign_element(const HtmlToken& token, ElementNamespace ns);
    void insert_character(char c);
    void insert_text(const std::string& text);
    void insert_comment(const HtmlToken& token);
    void insert_comment_at(const HtmlToken& token, const std::shared_ptr<Node>& parent,
                           const std::shared_ptr<Node>& before);
    void append_node_at(const std::shared_ptr<Node>& node, InsertionPlace place);

    // --- active formatting elements ----------------------------------------
    struct FormattingEntry {
        std::shared_ptr<Element> element;
        HtmlToken token;      // for reconstruction
        bool marker{false};
    };
    void push_formatting_element(const std::shared_ptr<Element>&, const HtmlToken&);
    void push_formatting_marker();
    void reconstruct_formatting_elements();
    int formatting_entry_index(const std::shared_ptr<Element>&) const;
    FormattingEntry* last_formatting_entry_by_tag(const std::string& tag);
    void clear_formatting_to_marker();
    bool adoption_agency(const HtmlToken& token);

    // --- misc ---------------------------------------------------------------
    void reset_insertion_mode();
    void set_tokenizer_rcdata(const std::string& tag);
    void set_tokenizer_rawtext(const std::string& tag);
    void set_tokenizer_script_data(const std::string& tag);
    void switch_to_text_mode(const std::string& tag, ContentModel model);
    bool is_mathml_text_integration_point(const Element&) const;
    bool is_html_integration_point(const Element&) const;
    void acknowledge_self_closing(const HtmlToken&);
    void close_p_element();
    void stop_parsing();

    static QuirksMode quirks_mode_for(const Doctype&, bool force_quirks);

    std::shared_ptr<Document> m_document;
    HtmlTokenizer* m_tokenizer{nullptr};
    std::vector<std::shared_ptr<Element>> m_open_elements;
    // Stack-indexed HTML tag positions and scope boundaries make scope queries
    // independent of stack depth. Rebuilt only for rare middle edits to the
    // tree-construction stack (e.g. adoption-agency repairs).
    std::unordered_map<std::string, std::vector<std::size_t>> m_open_html_tag_indices;
    std::array<std::vector<std::size_t>, 3> m_scope_boundary_indices;
    std::vector<FormattingEntry> m_formatting;
    std::shared_ptr<Element> m_head_element;
    std::shared_ptr<Element> m_form_element;
    Mode m_mode{Mode::Initial};
    Mode m_original_mode{Mode::Initial};
    std::vector<Mode> m_template_modes;
    std::string m_pending_table_text;
    // Mode to return to once pending table character tokens have been flushed.
    // The spec says "in table", but restoring the real mode keeps consecutive
    // rows in a single <tbody> (matching every browser) instead of spawning a
    // new row group per row.
    Mode m_table_text_return_mode{Mode::InTable};
    bool m_frameset_ok{true};
    bool m_foster_parenting{false};
    bool m_scripting{true};
    bool m_fragment_case{false};
    std::shared_ptr<Element> m_context_element;
    std::shared_ptr<Node> m_fragment_root;
    std::shared_ptr<Document> m_fragment_document;
    ContentModel m_tokenizer_pending{ContentModel::Data};
    std::string m_tokenizer_pending_tag;
    bool m_skip_next_newline{false};
};

} // namespace litebird
