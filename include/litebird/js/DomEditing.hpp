#pragma once
#include <string>

namespace litebird {
class Element;
namespace dom {
// Single text-node HTML editing hosts only. Never flatten formatted content.
bool plain_editable(const Element& host);
bool select_edit_text(Element& host, bool select_all);
bool edit_selected_text(Element& host, const std::string& inserted, bool backspace);
}
}
