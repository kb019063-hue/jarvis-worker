#pragma once

#include "litebird/dom/Node.hpp"
#include <string>

namespace litebird {

/// A DOM comment node (`<!-- data -->`), per the WHATWG DOM standard.
class Comment : public Node {
public:
    explicit Comment(std::string data)
        : Node(NodeType::Comment), m_data(std::move(data)) {}

    const std::string& data() const { return m_data; }
    void set_data(std::string data) { m_data = std::move(data); }

    std::string text_content() const override { return m_data; }

protected:
    void dump_open(std::ostream& os, const std::string& indent, bool has_content) const override;

private:
    std::string m_data;
};

} // namespace litebird
