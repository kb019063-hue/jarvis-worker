#pragma once

#include "litebird/dom/Node.hpp"
#include <string>

namespace litebird {

class Text : public Node {
public:
    explicit Text(std::string text)
        : Node(NodeType::Text), m_text(std::move(text)) {}

    const std::string& text() const { return m_text; }
    void set_text(std::string text) { m_text = std::move(text); }
    /// In-place append, used by the parser so inserting character data stays
    /// linear instead of copying the accumulated node on every character.
    void append_text(const std::string& text) { m_text.append(text); }
    void append_text(char c) { m_text.push_back(c); }

    std::string text_content() const override {
        return m_text;
    }

    void dump(std::ostream& os, int indent = 0) const override;

private:
    std::string m_text;
};

} // namespace litebird
