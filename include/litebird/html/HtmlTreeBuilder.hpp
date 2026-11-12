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

    /// How deep the parser nests elements: <html> is at depth 1, and nothing
    /// the parser attaches is deeper than this. An element that would be is
    /// attached to the nearest ancestor that can still take it, so it becomes
    /// a sibling of the element it was written inside (see
    /// within_depth_limit).
    ///
    /// The HTML standard has no such limit. Blink and WebKit both have one,
    /// with this number: `kMaximumHTMLParserDOMTreeDepth` in Blink
    /// (`HTMLConstructionSite::Attach`) and
    /// `defaultMaximumHTMLParserDOMTreeDepth` in WebKit
    /// (`HTMLConstructionSite::attachLater`). The rule is Blink's: only the
    /// place a node is attached changes. The stack of open elements, the
    /// insertion mode and everything else the tree builder keeps are what the
    /// standard says they are, at any depth.
    static constexpr std::size_t kMaxTreeDepth = 512;

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
    /// Removes an element from the middle of the stack. Unless
    /// `descendants_moved` says the tree was rearranged to match (the
    /// adoption agency algorithm does that), the elements above it stay
    /// inside it in the tree, one level deeper than their place on the stack
    /// suggests: </form> does this, and <a> inside an <a> that cannot be
    /// closed. within_depth_limit() needs to know how often that happened.
    void erase_open_element(std::size_t index, bool descendants_moved = false);
    void insert_open_element(std::size_t index, const std::shared_ptr<Element>& element);
    void replace_open_element(std::size_t index, const std::shared_ptr<Element>& element);
    // The indexes below describe the stack of open elements; these keep them
    // in step with it. An edit in the middle of the stack (the adoption
    // agency algorithm makes several) takes the entries from that point up
    // out of the indexes and puts them back afterwards, which costs the
    // distance from the top of the stack, not its length.
    void index_open_element(std::size_t index, const std::shared_ptr<Element>& element);
    void unindex_open_element(std::size_t index, const std::shared_ptr<Element>& element);
    void unindex_open_elements_from(std::size_t index);
    void index_open_elements_from(std::size_t index);
    /// Where `element` is on the stack of open elements, or -1.
    int open_element_position(const std::shared_ptr<Element>& element) const;
    void pop_until_tag(const std::string& tag);
    void pop_until_one_of(const std::vector<std::string>& tags);
    bool has_element_in_scope(const std::string& tag, Scope scope = Scope::Default) const;
    bool has_element_in_scope_where(const std::vector<std::string>& tags, Scope scope) const;
    bool has_element_in_table_scope(const std::string& tag) const;
    bool has_element_in_select_scope(const std::string& tag) const;
    int element_index_in_stack(const std::string& tag) const;
    /// For a start tag li, dd or dt: the name of the open li (or dd, dt)
    /// that it closes, or empty. The standard's walk down the stack to the
    /// first of them, stopped by a special element other than address, div
    /// and p.
    std::string open_list_item_closed_by(const std::string& name) const;
    /// "Any other end tag": the index of the open element with that name
    /// that the tag closes, or -1 when a special element is in the way.
    int open_element_closed_by_end_tag(const std::string& name) const;
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
    /// Where a node the parser has just created goes: the appropriate place
    /// for inserting a node, moved up the tree if that place is too deep.
    /// Used for elements and comments; text always goes into the current
    /// node, as in WebKit and Blink.
    InsertionPlace place_for_new_node();
    /// `place`, or the nearest place above it where a node can be attached
    /// so that it, and the `levels_below` levels of elements it brings with
    /// it (the chain of clones the adoption agency builds), end up no deeper
    /// than `ceiling`.
    InsertionPlace within_depth_limit(InsertionPlace place, std::size_t levels_below = 0,
                                      std::size_t ceiling = kMaxTreeDepth) const;
    /// The node above `node` when counting depth: its parent, or, for the
    /// contents of a template, the template.
    std::shared_ptr<Node> node_above(const Node& node) const;
    /// How many elements there are from the root down to and including
    /// `node`. Walks up; only called when near_depth_limit() says it matters.
    std::size_t element_depth(const Node& node) const;
    /// False while so few elements are open that nothing can be near the
    /// limit, which is the case for every ordinary page.
    bool near_depth_limit(std::size_t levels_below = 0) const {
        return m_open_elements.size() + m_depth_beyond_stack + 1 + levels_below > kMaxTreeDepth;
    }
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
    // Stack-indexed HTML tag positions, scope boundaries and the position of
    // each open element make the questions the tree builder asks about the
    // stack independent of its depth. The stack is as long as the markup is
    // deep (the nesting limit bounds the tree, not the stack), so a walk down
    // it for each token is the difference between a page that parses in a
    // fraction of a second and one that takes minutes.
    std::unordered_map<std::string, std::vector<std::size_t>> m_open_html_tag_indices;
    // 0 to 2: the boundaries of the default, list item and button scopes.
    // 3: every element in the "special" category. 4: the same without
    // address, div and p. The last two answer the two questions the standard
    // settles by walking down the stack until a special element stops it.
    std::array<std::vector<std::size_t>, 5> m_scope_boundary_indices;
    std::unordered_map<const Element*, std::size_t> m_open_positions;
    // Open elements that are not HTML elements. While there are none, the
    // indexes above (which are about HTML elements) answer exactly.
    std::size_t m_open_foreign_elements{0};
    // The depth in the tree of each open element, 0 while not measured.
    // Filled in by element_depth(), which is only asked near the limit.
    mutable std::vector<std::size_t> m_open_depths;
    // How much deeper in the tree an open element can be than its place on
    // the stack (see erase_open_element). Never decreases: it only decides
    // whether within_depth_limit() has to measure.
    std::size_t m_depth_beyond_stack{0};
    std::vector<FormattingEntry> m_formatting;
    // The template each template-contents fragment belongs to. The fragment
    // has no pointer back; counting depth needs the way up.
    std::unordered_map<const Node*, std::weak_ptr<Element>> m_template_of_contents;
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
