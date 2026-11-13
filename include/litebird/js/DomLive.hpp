#pragma once

// Ranges and NodeIterators follow the tree.
//
// A Range is two boundary points, each a node and an offset into it. When a
// script changes the tree after it made the range, the DOM standard says
// where each boundary goes: nodes inserted before a boundary push it on, a
// boundary inside something that is removed lands where that was, text that
// is edited or split takes the boundaries in it along. A NodeIterator whose
// reference node is removed steps out of what is going away. Without this a
// range kept across an edit (a selection, a search highlight, an editor's
// cursor) points at the wrong text, or into a part of the tree that is gone.
//
// The realm keeps a weak list of the ranges and iterators that exist; these
// are called where a change happens. With none alive each is one lookup.
// The implementation is in DomTraversalBindings.cpp.

#include "litebird/js/DomBindings.hpp"

namespace litebird {
namespace dom {

/// After children of `parent` were removed and/or added. `previous` and
/// `next` are the siblings on either side of where that happened.
void live_note_children_changed(Node& parent, const NodeVector& added, const NodeVector& removed,
                                const std::shared_ptr<Node>& previous, const std::shared_ptr<Node>& next);

/// Before `count` UTF-16 units at `offset` of a text or comment node are
/// replaced by `added` units.
void live_note_data_replaced(Node& node, std::size_t offset, std::size_t count, std::size_t added);

/// Before all of a text or comment node's data is replaced.
void live_note_data_set(Node& node);

/// After `tail`, the part of `node` from `offset` on, was put in the tree as
/// its next sibling, and before `node` is cut down to its first part.
void live_note_text_split(Node& node, std::size_t offset, const std::shared_ptr<Node>& tail);

/// normalize(): before the text of `gone` is appended to `into`, which is
/// `length` units long, and `gone` is removed.
void live_note_text_merged(Node& gone, const std::shared_ptr<Node>& into, std::size_t length);

/// Sets a node's data and nothing else: for callers that have told the
/// ranges precisely what changes. set_character_data() is the one for
/// "all of it is replaced".
void store_character_data(Node& node, const std::string& data);

/// Text.splitText(): the new node that holds the data from `offset` on.
std::shared_ptr<Node> split_text_node(Realm& realm, Node& node, std::size_t offset);

} // namespace dom
} // namespace litebird
