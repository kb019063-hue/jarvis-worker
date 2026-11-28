#pragma once

#include "litebird/dom/Document.hpp"
#include "litebird/dom/Element.hpp"
#include "litebird/css/StyleResolver.hpp"
#include "litebird/layout/LayoutBox.hpp"
#include "litebird/layout/LayoutTree.hpp"
#include "litebird/render/PaintCommand.hpp"
#include "litebird/render/TerminalRenderer.hpp"
#include "litebird/js/JsInterpreter.hpp"
#include "litebird/js/EventLoop.hpp"
#include "litebird/net/ResourceLoader.hpp"
#include <vector>
#include <string>
#include <memory>
#include <chrono>

namespace litebird {

struct InteractiveElement {
    std::shared_ptr<Element> element;
    Rect bounds;
    std::string tag;
    std::string text;
    std::string href;
    bool is_button{false};
    bool is_link{false};
    bool is_input{false};
};

class BrowserShell {
public:
    BrowserShell(float viewport_w = 800.0f, float viewport_h = 600.0f, int term_cols = 80, int term_rows = 24);
    ~BrowserShell() = default;

    // Navigation API
    bool navigate_to(const std::string& url);
    bool reload();
    bool back();
    bool forward();
    bool can_go_back() const;
    bool can_go_forward() const;

    const std::string& current_url() const { return m_current_url; }
    const std::vector<std::string>& history() const { return m_history; }
    size_t history_index() const { return m_history_index; }

    // DOM & Layout Accessors
    std::shared_ptr<Document> document() const { return m_document; }
    std::shared_ptr<LayoutBox> layout_root() const { return m_layout_root; }
    JsInterpreter* js_engine() { return m_js_engine.get(); }
    float content_height() const { return m_content_height; }

    // Viewport & Scrolling
    float viewport_width() const { return m_viewport_w; }
    float viewport_height() const { return m_viewport_h; }
    void set_viewport_size(float width, float height);
    const std::string& status_message() const { return m_status_message; }
    float scroll_y() const { return m_scroll_y; }
    float scroll_x() const { return m_scroll_x; }
    void scroll_to(float x, float y);
    void scroll_by(float dx, float dy);
    void scroll_up(float lines = 2.0f);
    void scroll_down(float lines = 2.0f);
    void scroll_page_up();
    void scroll_page_down();

    // Interactive Element Management & Hit Testing
    const std::vector<InteractiveElement>& interactive_elements() const { return m_interactive_elements; }
    int focused_element_index() const { return m_focused_index; }
    std::shared_ptr<Element> focused_element() const;
    /// Move both the ring and the document's focus, so `focus`/`blur` and
    /// `focusin`/`focusout` reach the page's listeners and
    /// `document.activeElement` follows the ring.
    void set_document_focus(const std::shared_ptr<Element>& element);
    void focus_next();
    void focus_prev();
    void focus_element(int index);
    /// A focused element that a dialog took away, or that left the document,
    /// is behind the ring's index. Drop it, so the next Tab starts from the
    /// document's own focus rather than from a stale index.
    void forget_focus_if_gone();
    bool activate_focused();
    bool click_at(float x, float y);
    /// A click through the DOM's own activation path, so the element's
    /// activation behavior runs (see `Element::click`).
    bool dispatch_click(const std::shared_ptr<Element>& target);
    bool hover_at(float x, float y);
    bool type_text(const std::string& text);
    bool handle_key(const std::string& key);
    std::shared_ptr<Element> element_at(float x, float y) const;

    // Tick & Event Loop
    size_t tick(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    bool is_dirty() const { return m_dirty; }
    void mark_dirty() { m_dirty = true; }
    void relayout();

    // Graphical PNG & Terminal Frame Rendering
    std::vector<uint8_t> render_png_frame();
    std::string render_terminal_frame(bool devtools = false);
    void print_terminal_frame(std::ostream& os = std::cout, bool devtools = false);

    // Interactive TUI Main Loop (runs until 'q' or exit)
    void run_interactive();

private:
    void show_page(const std::string& html, const std::string& content_type = "text/html");
    void rebuild_layout();
    void scan_interactive_elements();
    Rect compute_element_bounds(const std::shared_ptr<Element>& el, const std::shared_ptr<LayoutBox>& box) const;

    float m_viewport_w{800.0f};
    float m_viewport_h{600.0f};
    int m_term_cols{80};
    int m_term_rows{24};

    float m_scroll_x{0.0f};
    float m_scroll_y{0.0f};
    float m_content_height{0.0f};
    bool m_dirty{false};

    std::string m_current_url;
    std::vector<std::string> m_history;
    size_t m_history_index{0};

    std::shared_ptr<Document> m_document;
    StyleResolver m_resolver;
    std::shared_ptr<LayoutBox> m_layout_root;
    DisplayList m_display_list;
    std::unique_ptr<JsInterpreter> m_js_engine;

    std::vector<InteractiveElement> m_interactive_elements;
    int m_focused_index{-1};
    bool m_keyboard_focus{false};
    std::string m_status_message{"Ready"};
};

} // namespace litebird
