#include "litebird/dom/Comment.hpp"
#include "litebird/dom/ProcessingInstruction.hpp"
#include "litebird/common/TerminalText.hpp"
#include <ostream>

namespace litebird {

void Comment::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "<!-- " << terminal_safe(m_data) << " -->\n";
}

void ProcessingInstruction::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "<?" << m_target << " " << terminal_safe(m_data) << "?>\n";
}

} // namespace litebird
