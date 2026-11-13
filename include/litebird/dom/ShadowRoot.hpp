#pragma once

#include "litebird/dom/Node.hpp"
#include "litebird/css/CssParser.hpp"
#include <string>
#include <vector>
#include <memory>

namespace litebird {

class Element;

enum class ShadowRootMode {
    Open,
    Closed
};

class ShadowRoot : public Node {
public:
    ShadowRoot(std::shared_ptr<Element> host, ShadowRootMode mode);

    std::shared_ptr<Element> host() const { return m_host.lock(); }
    ShadowRootMode mode() const { return m_mode; }
    std::string mode_str() const { return m_mode == ShadowRootMode::Open ? "open" : "closed"; }

    std::string inner_html() const;
    void set_inner_html(const std::string& html);

    std::shared_ptr<Element> get_element_by_id(const std::string& id) const;
    std::shared_ptr<Element> query_selector(const std::string& selector) const;
    std::vector<std::shared_ptr<Element>> query_selector_all(const std::string& selector) const;

    const std::vector<std::string>& stylesheets() const { return m_stylesheets; }
    const std::vector<StyleSheet>& parsed_stylesheets() const { return m_parsed_stylesheets; }
    void add_stylesheet(std::string css);

    // Slot distribution helpers
    static std::vector<std::shared_ptr<Node>> assigned_nodes(const std::shared_ptr<Element>& slot_el);
    static std::vector<std::shared_ptr<Element>> assigned_elements(const std::shared_ptr<Element>& slot_el);

protected:
    void dump_open(std::ostream& os, const std::string& indent, bool has_content) const override;

private:
    std::weak_ptr<Element> m_host;
    ShadowRootMode m_mode{ShadowRootMode::Open};
    std::vector<std::string> m_stylesheets;
    std::vector<StyleSheet> m_parsed_stylesheets;
};

} // namespace litebird
