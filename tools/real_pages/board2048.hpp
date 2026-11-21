#pragma once

#include "litebird/dom/Document.hpp"
#include <array>
#include <optional>
#include <sstream>

namespace board2048 {
using Board = std::array<int, 16>; // row-major, zero means empty

// Read the tiles the application placed on the page, never its private game
// model. A merge adds transient tiles before the final tile at the same spot.
inline std::optional<Board> read(const litebird::Element& container) {
    Board board{};
    for (const auto& node : container.children()) {
        if (!node->is_element()) continue;
        auto tile = node->as_element();
        if (!tile->has_class("tile")) continue;
        std::istringstream classes(tile->get_attribute("class"));
        std::string token;
        bool found = false;
        while (classes >> token) {
            if (!token.starts_with("tile-position-")) continue;
            auto xy = token.substr(14);
            if (xy.size()!=3 || xy[1]!='-' || xy[0]<'1' || xy[0]>'4' || xy[2]<'1' || xy[2]>'4') return {};
            auto text = tile->text_content();
            // Only 2, 4 and 8 are possible after one opening move.
            int value = text=="2" ? 2 : text=="4" ? 4 : text=="8" ? 8 : 0;
            if (!value) return {};
            board[(xy[2]-'1')*4 + xy[0]-'1'] = value;
            found = true;
        }
        if (!found) return {};
    }
    return board;
}

inline Board slide(const Board& before, std::string_view key) {
    Board result{};
    for (int line=0; line<4; ++line) {
        auto at = [&](int i) {
            if (key=="ArrowRight" || key=="ArrowDown") i=3-i;
            return key=="ArrowLeft" || key=="ArrowRight" ? 4*line+i : 4*i+line;
        };
        int used=0, pending=0;
        for (int i=0; i<4; ++i) {
            int v=before[at(i)];
            if (!v) continue;
            if (pending && v==pending) { result[at(used++)]=2*v; pending=0; }
            else { if (pending) result[at(used++)]=pending; pending=v; }
        }
        if (pending) result[at(used)]=pending;
    }
    return result;
}

inline bool is_move(const Board& before, const Board& after, std::string_view key) {
    const Board expected=slide(before,key);
    if (expected==before) return false;
    int spawned=0;
    for (int i=0; i<16; ++i) {
        if (expected[i]==after[i]) continue;
        if (expected[i]!=0 || (after[i]!=2 && after[i]!=4)) return false;
        ++spawned;
    }
    return spawned==1;
}

inline std::string describe(const Board& b) {
    std::string out;
    for (int i=0; i<16; ++i) {
        if (i && !(i%4)) out+='/';
        out+=b[i]?std::to_string(b[i]):".";
    }
    return out;
}
}
