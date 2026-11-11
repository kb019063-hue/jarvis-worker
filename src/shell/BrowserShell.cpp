#include "litebird/shell/BrowserShell.hpp"
#include "litebird/html/HtmlParser.hpp"
#include "litebird/css/CssParser.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstring>

#include <termios.h>
#include <unistd.h>
#include <sys/select.h>

namespace litebird {

namespace {

struct RawTerminalMode {
    struct termios orig_termios;
    bool enabled{false};

    RawTerminalMode() {
        if (!isatty(STDIN_FILENO)) return;
        if (tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
            struct termios raw = orig_termios;
            raw.c_lflag &= ~(ECHO | ICANON);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 1; // 100ms
            if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
                enabled = true;
            }
        }
    }

    ~RawTerminalMode() {
        if (enabled) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
            std::cout << "\033[?25h" << std::flush; // Ensure cursor is visible
        }
    }
};

} // namespace

BrowserShell::BrowserShell(float viewport_w, float viewport_h, int term_cols, int term_rows)
    : m_viewport_w(viewport_w), m_viewport_h(viewport_h),
      m_term_cols(term_cols), m_term_rows(term_rows) {}

bool BrowserShell::can_go_back() const {
    return m_history_index > 0 && !m_history.empty();
}

bool BrowserShell::can_go_forward() const {
    return !m_history.empty() && m_history_index + 1 < m_history.size();
}

bool BrowserShell::back() {
    if (!can_go_back()) return false;
    m_history_index--;
    std::string prev_url = m_history[m_history_index];

    // Load without altering history vector
    auto resp = ResourceLoader::load(prev_url);
    if (!resp.success) {
        m_status_message = "Failed to load: " + resp.error_message;
        return false;
    }

    m_current_url = prev_url;
    m_document = HtmlParser::parse(resp.body);

    m_resolver = StyleResolver();
    for (const auto& el : m_document->get_elements_by_tag_name("style")) {
        auto sheet = CssParser::parse(el->text_content());
        m_resolver.add_stylesheet(sheet);
    }

    m_js_engine = std::make_unique<JsInterpreter>(m_document);
    for (const auto& el : m_document->get_elements_by_tag_name("script")) {
        std::string script = el->text_content();
        if (!script.empty()) {
            m_js_engine->eval_ast(script);
        }
    }
    m_js_engine->notify_document_loaded();

    m_scroll_y = 0.0f;
    m_scroll_x = 0.0f;
    rebuild_layout();
    m_status_message = "Navigated Back to " + m_current_url;
    m_dirty = true;
    return true;
}

bool BrowserShell::forward() {
    if (!can_go_forward()) return false;
    m_history_index++;
    std::string next_url = m_history[m_history_index];

    auto resp = ResourceLoader::load(next_url);
    if (!resp.success) {
        m_status_message = "Failed to load: " + resp.error_message;
        return false;
    }

    m_current_url = next_url;
    m_document = HtmlParser::parse(resp.body);

    m_resolver = StyleResolver();
    for (const auto& el : m_document->get_elements_by_tag_name("style")) {
        auto sheet = CssParser::parse(el->text_content());
        m_resolver.add_stylesheet(sheet);
    }

    m_js_engine = std::make_unique<JsInterpreter>(m_document);
    for (const auto& el : m_document->get_elements_by_tag_name("script")) {
        std::string script = el->text_content();
        if (!script.empty()) {
            m_js_engine->eval_ast(script);
        }
    }
    m_js_engine->notify_document_loaded();

    m_scroll_y = 0.0f;
    m_scroll_x = 0.0f;
    rebuild_layout();
    m_status_message = "Navigated Forward to " + m_current_url;
    m_dirty = true;
    return true;
}

bool BrowserShell::reload() {
    if (m_current_url.empty()) return false;
    auto resp = ResourceLoader::load(m_current_url);
    if (!resp.success) {
        m_status_message = "Reload failed: " + resp.error_message;
        return false;
    }

    m_document = HtmlParser::parse(resp.body);
    m_resolver = StyleResolver();
    for (const auto& el : m_document->get_elements_by_tag_name("style")) {
        auto sheet = CssParser::parse(el->text_content());
        m_resolver.add_stylesheet(sheet);
    }

    m_js_engine = std::make_unique<JsInterpreter>(m_document);
    for (const auto& el : m_document->get_elements_by_tag_name("script")) {
        std::string script = el->text_content();
        if (!script.empty()) {
            m_js_engine->eval_ast(script);
        }
    }
    m_js_engine->notify_document_loaded();

    rebuild_layout();
    m_status_message = "Reloaded " + m_current_url;
    m_dirty = true;
    return true;
}

bool BrowserShell::navigate_to(const std::string& url) {
    auto resp = ResourceLoader::load(url);
    if (!resp.success) {
        m_status_message = "Error loading " + url + ": " + resp.error_message;
        std::string error_html = "<html><head><title>Error</title><style>body{font-size:16px;color:#ef4444;background-color:#0f172a;margin:20px;}h1{color:#f87171;}</style></head><body><h1>Navigation Error</h1><p>" + resp.error_message + "</p></body></html>";
        m_document = HtmlParser::parse(error_html);
        m_resolver = StyleResolver();
        m_js_engine = std::make_unique<JsInterpreter>(m_document);
        rebuild_layout();
        m_dirty = true;
        return false;
    }

    // Update history
    if (!m_history.empty() && m_history_index + 1 < m_history.size()) {
        m_history.erase(m_history.begin() + m_history_index + 1, m_history.end());
    }
    m_history.push_back(url);
    m_history_index = m_history.size() - 1;
    m_current_url = url;

    // Parse HTML
    m_document = HtmlParser::parse(resp.body);

    // CSS Styling
    m_resolver = StyleResolver();
    for (const auto& el : m_document->get_elements_by_tag_name("style")) {
        auto sheet = CssParser::parse(el->text_content());
        m_resolver.add_stylesheet(sheet);
    }

    // JS Engine & Scripts
    m_js_engine = std::make_unique<JsInterpreter>(m_document);
    for (const auto& el : m_document->get_elements_by_tag_name("script")) {
        std::string script = el->text_content();
        if (!script.empty()) {
            m_js_engine->eval_ast(script);
        }
    }
    m_js_engine->notify_document_loaded();

    m_scroll_y = 0.0f;
    m_scroll_x = 0.0f;
    rebuild_layout();
    m_status_message = "Loaded " + url;
    m_dirty = true;
    return true;
}

void BrowserShell::rebuild_layout() {
    if (!m_document) return;

    m_layout_root = LayoutTree::build(m_document, m_resolver, m_viewport_w, m_viewport_h);
    if (m_layout_root) {
        m_display_list = DisplayList::build(m_layout_root);
        m_content_height = m_layout_root->dimensions.border_box().height;
    } else {
        m_content_height = 0.0f;
    }

    scan_interactive_elements();
}

void BrowserShell::relayout() {
    rebuild_layout();
    m_dirty = true;
}

Rect BrowserShell::compute_element_bounds(const std::shared_ptr<Element>& el, const std::shared_ptr<LayoutBox>& box) const {
    if (!box || !el) return Rect();

    Rect result;
    bool found = false;

    if (box->node == el) {
        result = box->dimensions.border_box();
        found = true;
    }

    // Check inline line box fragments
    for (const auto& line : box->line_boxes) {
        for (const auto& frag : line.fragments) {
            if (frag.box && frag.box->node == el) {
                if (!found) {
                    result = frag.rect;
                    found = true;
                } else {
                    float min_x = std::min(result.x, frag.rect.x);
                    float min_y = std::min(result.y, frag.rect.y);
                    float max_x = std::max(result.x + result.width, frag.rect.x + frag.rect.width);
                    float max_y = std::max(result.y + result.height, frag.rect.y + frag.rect.height);
                    result = Rect(min_x, min_y, max_x - min_x, max_y - min_y);
                }
            }
        }
    }

    // Recurse into children
    for (const auto& ch : box->children) {
        Rect ch_rect = compute_element_bounds(el, ch);
        if (ch_rect.width > 0 && ch_rect.height > 0) {
            if (!found) {
                result = ch_rect;
                found = true;
            } else {
                float min_x = std::min(result.x, ch_rect.x);
                float min_y = std::min(result.y, ch_rect.y);
                float max_x = std::max(result.x + result.width, ch_rect.x + ch_rect.width);
                float max_y = std::max(result.y + result.height, ch_rect.y + ch_rect.height);
                result = Rect(min_x, min_y, max_x - min_x, max_y - min_y);
            }
        }
    }

    return result;
}

void BrowserShell::scan_interactive_elements() {
    m_interactive_elements.clear();
    if (!m_document || !m_layout_root) return;

    std::vector<std::shared_ptr<Element>> candidates;

    // Helper DOM visitor
    std::function<void(const std::shared_ptr<Node>&)> visitor = [&](const std::shared_ptr<Node>& n) {
        if (!n) return;
        if (auto el = std::dynamic_pointer_cast<Element>(n)) {
            std::string tag = el->tag_name();
            std::string cls = el->get_attribute("class");
            std::string id = el->id();
            bool is_link = (tag == "a" && el->has_attribute("href"));
            bool is_btn = (tag == "button" || cls.find("btn") != std::string::npos || id.find("btn") != std::string::npos || el->has_listeners("click"));
            bool is_inp = (tag == "input");

            if (is_link || is_btn || is_inp) {
                candidates.push_back(el);
            }
        }
        for (const auto& ch : n->children()) {
            visitor(ch);
        }
    };

    visitor(m_document);

    for (const auto& el : candidates) {
        Rect bounds = compute_element_bounds(el, m_layout_root);
        InteractiveElement item;
        item.element = el;
        item.bounds = bounds;
        item.tag = el->tag_name();
        item.text = el->text_content();
        item.href = el->get_attribute("href");
        item.is_link = (item.tag == "a" && !item.href.empty());
        item.is_button = (item.tag == "button" || el->get_attribute("class").find("btn") != std::string::npos || el->has_listeners("click"));
        item.is_input = (item.tag == "input");

        m_interactive_elements.push_back(std::move(item));
    }

    if (!m_interactive_elements.empty()) {
        if (m_focused_index < 0 || m_focused_index >= static_cast<int>(m_interactive_elements.size())) {
            m_focused_index = 0;
        }
    } else {
        m_focused_index = -1;
    }
}

std::shared_ptr<Element> BrowserShell::focused_element() const {
    if (m_focused_index >= 0 && m_focused_index < static_cast<int>(m_interactive_elements.size())) {
        return m_interactive_elements[m_focused_index].element;
    }
    return nullptr;
}

void BrowserShell::focus_next() {
    if (m_interactive_elements.empty()) return;
    m_focused_index = (m_focused_index + 1) % m_interactive_elements.size();
    m_status_message = "Focused: <" + m_interactive_elements[m_focused_index].tag + "> \"" + m_interactive_elements[m_focused_index].text + "\"";
    m_dirty = true;
}

void BrowserShell::focus_prev() {
    if (m_interactive_elements.empty()) return;
    m_focused_index = (m_focused_index - 1 + m_interactive_elements.size()) % m_interactive_elements.size();
    m_status_message = "Focused: <" + m_interactive_elements[m_focused_index].tag + "> \"" + m_interactive_elements[m_focused_index].text + "\"";
    m_dirty = true;
}

void BrowserShell::focus_element(int index) {
    if (index >= 0 && index < static_cast<int>(m_interactive_elements.size())) {
        m_focused_index = index;
        m_dirty = true;
    }
}

bool BrowserShell::activate_focused() {
    auto el = focused_element();
    if (!el) return false;

    const auto& item = m_interactive_elements[m_focused_index];
    if (item.is_link && !item.href.empty()) {
        std::string next_url = ResourceLoader::resolve_url(m_current_url, item.href);
        m_status_message = "Following link to " + next_url;
        return navigate_to(next_url);
    }

    // Trigger click on element
    m_status_message = "Clicked <" + item.tag + "> #" + el->id();
    el->click();
    rebuild_layout();
    m_dirty = true;
    return true;
}

std::shared_ptr<Element> BrowserShell::element_at(float x, float y) const {
    if (!m_layout_root) return nullptr;

    float adjusted_y = y + m_scroll_y;
    float adjusted_x = x + m_scroll_x;

    std::shared_ptr<Element> hit_element = nullptr;

    std::function<void(const std::shared_ptr<LayoutBox>&)> search = [&](const std::shared_ptr<LayoutBox>& box) {
        if (!box) return;
        Rect b_rect = box->dimensions.border_box();
        bool inside = false;
        if (box->style.has_transform && !box->style.transform_matrix.is_identity()) {
            Point local_p = box->style.transform_matrix.inverse().map_point(adjusted_x, adjusted_y);
            inside = b_rect.contains(local_p.x, local_p.y);
        } else {
            inside = b_rect.contains(adjusted_x, adjusted_y);
        }

        if (inside) {
            if (box->node) {
                if (auto el = std::dynamic_pointer_cast<Element>(box->node)) {
                    hit_element = el;
                } else if (box->node->parent()) {
                    hit_element = std::dynamic_pointer_cast<Element>(box->node->parent());
                }
            }
            // Recurse into children
            for (const auto& ch : box->children) {
                search(ch);
            }
        }
    };

    search(m_layout_root);
    return hit_element;
}

bool BrowserShell::click_at(float x, float y) {
    auto el = element_at(x, y);
    if (!el) return false;

    // Check if element is a link
    std::shared_ptr<Node> curr = el;
    while (curr) {
        if (auto elem = std::dynamic_pointer_cast<Element>(curr)) {
            if (elem->tag_name() == "a" && elem->has_attribute("href")) {
                std::string target = elem->get_attribute("href");
                std::string next_url = ResourceLoader::resolve_url(m_current_url, target);
                return navigate_to(next_url);
            }
        }
        curr = curr->parent();
    }

    el->click();
    rebuild_layout();
    m_dirty = true;
    return true;
}

void BrowserShell::scroll_to(float x, float y) {
    m_scroll_x = std::max(0.0f, x);
    float max_y = std::max(0.0f, m_content_height - (m_term_rows - 3) * 20.0f);
    m_scroll_y = std::clamp(y, 0.0f, max_y);
    m_dirty = true;
}

void BrowserShell::scroll_by(float dx, float dy) {
    scroll_to(m_scroll_x + dx, m_scroll_y + dy);
}

void BrowserShell::scroll_up(float lines) {
    scroll_by(0.0f, -lines * 20.0f);
}

void BrowserShell::scroll_down(float lines) {
    scroll_by(0.0f, lines * 20.0f);
}

void BrowserShell::scroll_page_up() {
    scroll_by(0.0f, -static_cast<float>(m_term_rows - 4) * 20.0f);
}

void BrowserShell::scroll_page_down() {
    scroll_by(0.0f, static_cast<float>(m_term_rows - 4) * 20.0f);
}

size_t BrowserShell::tick(std::chrono::steady_clock::time_point now) {
    size_t executed = EventLoop::default_loop().tick(now);
    if (executed > 0) {
        rebuild_layout();
        m_dirty = true;
    }
    return executed;
}

std::string BrowserShell::render_terminal_frame(bool devtools) {
    std::ostringstream out;

    // 1. Navigation Header Bar (Line 0)
    out << "\033[48;2;30;41;59m\033[38;2;255;255;255m"; // Dark slate background, white text
    out << (can_go_back() ? "\033[1;32m[◄ Back]\033[0;48;2;30;41;59m\033[38;2;255;255;255m " : "\033[38;2;100;116;139m[◄ Back]\033[38;2;255;255;255m ");
    out << (can_go_forward() ? "\033[1;32m[► Fwd]\033[0;48;2;30;41;59m\033[38;2;255;255;255m " : "\033[38;2;100;116;139m[► Fwd]\033[38;2;255;255;255m ");
    out << "\033[1;34m[⟳ Reload]\033[0;48;2;30;41;59m\033[38;2;255;255;255m ";
    out << "URL: [\033[1m" << (m_current_url.empty() ? "about:blank" : m_current_url) << "\033[22m] ";
    out << "\033[0m\n";

    // 2. Viewport Content
    int content_rows = devtools ? std::max(6, m_term_rows - 8) : std::max(6, m_term_rows - 3);
    TerminalRenderer renderer(m_term_cols, content_rows, 10.0f, 20.0f);
    renderer.set_scroll(m_scroll_x, m_scroll_y);

    // If an element is focused, draw active highlight ring
    if (m_focused_index >= 0 && m_focused_index < static_cast<int>(m_interactive_elements.size())) {
        renderer.draw_focus_ring(m_interactive_elements[m_focused_index].bounds, Color(245, 158, 11)); // Amber
    }

    out << renderer.to_ansi_string(m_display_list);

    // 3. Status Bar
    int scroll_pct = (m_content_height > 0.0f) ?
        static_cast<int>((m_scroll_y / std::max(1.0f, m_content_height - content_rows * 20.0f)) * 100.0f) : 0;
    scroll_pct = std::clamp(scroll_pct, 0, 100);

    out << "\033[48;2;15;23;42m\033[38;2;148;163;184m"; // Dark slate background, light text
    out << "Scroll: " << std::setw(3) << scroll_pct << "% | ";
    out << "Links/Btns: " << m_interactive_elements.size() << " | ";
    if (m_focused_index >= 0 && m_focused_index < static_cast<int>(m_interactive_elements.size())) {
        const auto& item = m_interactive_elements[m_focused_index];
        out << "Focus: <" << item.tag;
        if (!item.element->id().empty()) out << "#" << item.element->id();
        out << "> | ";
    }
    out << m_status_message;
    out << "\033[0m\n";

    // 4. Shortcut Help Bar
    out << "\033[48;2;2;6;23m\033[38;2;226;232;240m";
    out << "[Tab/S-Tab] Focus  [Enter] Click/Go  [↑/↓/PgUp/PgDn] Scroll  [o] Open  [b] Back  [d] DevTools  [q] Quit";
    out << "\033[0m\n";

    // 5. Optional DevTools Pane
    if (devtools) {
        // Rendering the DevTools pane used to collect with an EMPTY root set,
        // which freed every JsObject the page could still reach and left the
        // interpreter holding dangling pointers (reproduced as an ASan
        // heap-use-after-free). The collector now pulls roots from registered
        // providers, so this is safe -- and `stats` is the sane thing to
        // report, since collect_garbage() already returns them.
        auto heap_stats = Heap::default_heap().collect_garbage();
        (void)heap_stats;
        out << "\033[48;2;30;41;59m\033[38;2;251;191;36m";
        out << "--- [Litebird DevTools] ---\n";
        out << "DOM Elements: " << (m_document ? m_document->get_elements_by_tag_name("*").size() : 0)
            << " | Layout Boxes: " << (m_layout_root ? 1 + m_layout_root->children.size() : 0)
            << " | Content Height: " << m_content_height << "px\n";
        out << "Heap GC Cells: " << heap_stats.alive_cells << "/" << heap_stats.total_cells
            << " | Memory: " << heap_stats.bytes_allocated << " B"
            << " | Collections: " << heap_stats.collections_count << "\n";
        out << "\033[0m";
    }

    return out.str();
}

void BrowserShell::print_terminal_frame(std::ostream& os, bool devtools) {
    os << render_terminal_frame(devtools) << std::flush;
}

void BrowserShell::run_interactive() {
    RawTerminalMode raw_mode;
    if (!raw_mode.enabled) {
        std::cerr << "Warning: Terminal does not support raw mode; printing static frame\n";
        print_terminal_frame(std::cout, false);
        return;
    }

    std::cout << "\033[2J\033[H\033[?25l" << std::flush; // Clear screen, hide cursor

    bool running = true;
    bool devtools = false;
    m_dirty = true;

    while (running) {
        // Run timers
        tick();

        if (m_dirty) {
            std::cout << "\033[H" << std::flush; // Move cursor to top-left
            print_terminal_frame(std::cout, devtools);
            m_dirty = false;
        }

        // Wait for keyboard input with 50ms timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000; // 50ms

        int ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char ch = 0;
            if (read(STDIN_FILENO, &ch, 1) > 0) {
                if (ch == 'q' || ch == 3) { // 'q' or Ctrl+C
                    running = false;
                } else if (ch == '\t') { // Tab
                    focus_next();
                } else if (ch == '\r' || ch == '\n') { // Enter
                    activate_focused();
                } else if (ch == 'k') {
                    scroll_up(2.0f);
                } else if (ch == 'j') {
                    scroll_down(2.0f);
                } else if (ch == 'b') {
                    back();
                } else if (ch == 'f') {
                    forward();
                } else if (ch == 'r') {
                    reload();
                } else if (ch == 'd') {
                    devtools = !devtools;
                    m_dirty = true;
                } else if (ch == 'o' || ch == 'g') {
                    // Open URL prompt
                    std::cout << "\033[?25h\033[H\033[K" << std::flush; // Show cursor
                    std::cout << "\033[48;2;37;99;235m\033[38;2;255;255;255m Enter URL or File: \033[0m " << std::flush;
                    // Read line in canonical mode
                    struct termios canon = raw_mode.orig_termios;
                    tcsetattr(STDIN_FILENO, TCSANOW, &canon);
                    std::string input_url;
                    std::getline(std::cin, input_url);
                    // Restore raw mode
                    struct termios raw = raw_mode.orig_termios;
                    raw.c_lflag &= ~(ECHO | ICANON);
                    raw.c_cc[VMIN] = 0;
                    raw.c_cc[VTIME] = 1;
                    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
                    std::cout << "\033[?25l" << std::flush;

                    if (!input_url.empty()) {
                        navigate_to(input_url);
                    }
                } else if (ch == '\033') { // Escape sequence
                    char seq[3] = {0};
                    if (read(STDIN_FILENO, &seq[0], 1) > 0 && read(STDIN_FILENO, &seq[1], 1) > 0) {
                        if (seq[0] == '[') {
                            if (seq[1] == 'A') { // Up arrow
                                scroll_up(2.0f);
                            } else if (seq[1] == 'B') { // Down arrow
                                scroll_down(2.0f);
                            } else if (seq[1] == 'C') { // Right arrow
                                forward();
                            } else if (seq[1] == 'D') { // Left arrow
                                back();
                            } else if (seq[1] == 'Z') { // Shift+Tab
                                focus_prev();
                            } else if (seq[1] == '5') { // Page Up (~ followed)
                                char t; (void)read(STDIN_FILENO, &t, 1);
                                scroll_page_up();
                            } else if (seq[1] == '6') { // Page Down (~ followed)
                                char t; (void)read(STDIN_FILENO, &t, 1);
                                scroll_page_down();
                            }
                        }
                    }
                }
            }
        }
    }

    std::cout << "\033[2J\033[H\033[?25h" << std::flush; // Clear and restore cursor
}

} // namespace litebird
