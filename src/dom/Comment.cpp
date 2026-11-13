#include "litebird/dom/Comment.hpp"
#include <ostream>

namespace litebird {

void Comment::dump_open(std::ostream& os, const std::string& indent, bool) const {
    os << indent << "<!-- " << m_data << " -->\n";
}

} // namespace litebird
