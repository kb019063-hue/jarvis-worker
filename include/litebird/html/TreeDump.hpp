#pragma once

#include "litebird/dom/Node.hpp"
#include <iosfwd>
#include <string>

namespace litebird {

/// Serializes a DOM tree in the html5lib "tree dump" format used by the
/// web-platform test corpus:
///
///     | <html>
///     |   <head>
///     |   <body>
///     |     <div>
///     |       class="card"
///     |       "text"
///
/// `root`'s children are emitted at depth 0, which is what both the
/// `#document` and the `#document-fragment` sections of the corpus expect.
std::string dump_tree_webplatform(const Node& root);
void dump_tree_webplatform(std::ostream& os, const Node& root);

} // namespace litebird
