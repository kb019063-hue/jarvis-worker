#pragma once

#include "litebird/dom/Node.hpp"
#include <string>

namespace litebird {

/// A processing instruction (`<?xml-stylesheet href="a.css"?>`): a target
/// and data, and character data as far as the DOM is concerned.
class ProcessingInstruction : public Node {
public:
    ProcessingInstruction(std::string target, std::string data)
        : Node(NodeType::ProcessingInstruction), m_target(std::move(target)), m_data(std::move(data)) {}

    const std::string& target() const { return m_target; }
    const std::string& data() const { return m_data; }
    void set_data(std::string data) { m_data = std::move(data); }

    std::string text_content() const override { return m_data; }

protected:
    void dump_open(std::ostream& os, const std::string& indent, bool has_content) const override;

private:
    std::string m_target;
    std::string m_data;
};

} // namespace litebird
