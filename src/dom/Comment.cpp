#include "litebird/dom/Comment.hpp"
#include "litebird/dom/ProcessingInstruction.hpp"
#include <ostream>

namespace litebird {

void Comment::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "<!-- " << m_data << " -->\n";
}

void ProcessingInstruction::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "<?" << m_target << " " << m_data << "?>\n";
}

} // namespace litebird
