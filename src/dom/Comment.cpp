#include "litebird/dom/Comment.hpp"
#include <ostream>

namespace litebird {

void Comment::dump(std::ostream& os, int indent) const {
    std::string spaces(indent * 2, ' ');
    os << spaces << "<!-- " << m_data << " -->\n";
}

} // namespace litebird
