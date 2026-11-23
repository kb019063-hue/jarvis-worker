// Tiny terminal-only driver for verify_terminal_clipping.py. The expected cell
// grids and ANSI decoder live on the Python side, not in the renderer.
#include "litebird/render/TerminalRenderer.hpp"
#include <iostream>
#include <limits>
#include <string>

using namespace litebird;

static DrawBorderCommand border(Rect rect, EdgeSizes widths = EdgeSizes(1.0f)) {
    DrawBorderCommand cmd;
    cmd.rect = rect;
    cmd.width = widths;
    cmd.edge_colors = EdgeColors(Color::red(), Color(0, 255, 0), Color::blue(), Color(255, 255, 0));
    return cmd;
}

static DrawTextCommand text(float x, float y, std::string s, Color color = Color::black()) {
    DrawTextCommand cmd;
    cmd.position = Point(x, y);
    cmd.text = std::move(s);
    cmd.color = color;
    return cmd;
}

static DrawShadowCommand shadow(Rect rect, float dx, float dy, Color color) {
    DrawShadowCommand cmd;
    cmd.rect = rect;
    cmd.x_offset = dx;
    cmd.y_offset = dy;
    cmd.color = color;
    return cmd;
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string test = argv[1];
    if (test == "empty_viewport") {
        TerminalRenderer empty(0, 0, 10, 20);
        DisplayList commands;
        commands.add(DrawCanvasCommand{Color::white()});
        commands.add(border(Rect(0, 0, 20, 40)));
        commands.add(text(0, 0, "abc"));
        commands.add(shadow(Rect(0, 0, 20, 40), 0, 0, Color::red()));
        empty.draw_focus_ring(Rect(0, 0, 20, 40));
        std::cout << empty.to_ansi_string(commands);
        return 0;
    }

    TerminalRenderer terminal(5, 4, 10, 20);
    DisplayList commands;
    commands.add(DrawCanvasCommand{Color::white()});

    if (test == "border_exact") {
        commands.add(border(Rect(0, 0, 30, 60)));
    } else if (test == "border_partial") {
        commands.add(border(Rect(-10, -20, 40, 60)));
    } else if (test == "border_offscreen") {
        commands.add(border(Rect(0, -80, 50, 20)));
    } else if (test == "border_encloses_viewport") {
        commands.add(border(Rect(-1000000, -1000000, 2000000, 2000000)));
    } else if (test == "border_clipped") {
        commands.add(PushClipCommand{Rect(10, 20, 30, 40)});
        commands.add(border(Rect(0, 0, 50, 80)));
        commands.add(PopClipCommand{});
    } else if (test == "border_sides") {
        commands.add(border(Rect(0, 0, 30, 60), EdgeSizes(1, 0, 1, 0)));
    } else if (test == "border_clip_visible") {
        commands.add(PushClipCommand{Rect(10, 0, 20, 60)});
        commands.add(border(Rect(0, 0, 40, 60)));
        commands.add(PopClipCommand{});
    } else if (test == "border_transparent") {
        auto cmd = border(Rect(0, 0, 30, 60));
        cmd.edge_colors->top = Color::transparent();
        commands.add(cmd);
    } else if (test == "border_scrolled") {
        terminal.set_scroll(10, 20);
        commands.add(border(Rect(10, 20, 30, 60)));
    } else if (test == "text_clipped") {
        commands.add(PushClipCommand{Rect(10, 20, 20, 20)});
        commands.add(text(0, 20, "ABCDE"));
        commands.add(PopClipCommand{});
    } else if (test == "nested_text_clips") {
        commands.add(PushClipCommand{Rect(0, 0, 40, 80)});
        commands.add(PushClipCommand{Rect(20, 0, 20, 80)});
        commands.add(text(0, 20, "ABCDE"));
        commands.add(PopClipCommand{});
        commands.add(text(0, 40, "12345"));
        commands.add(PopClipCommand{});
    } else if (test == "transformed_clip") {
        commands.add(PushTransformCommand{TransformMatrix2D::translation(10, 20)});
        commands.add(PushClipCommand{Rect(0, 0, 20, 20)});
        commands.add(text(0, 0, "ABCD"));
        commands.add(PopClipCommand{});
        commands.add(PopTransformCommand{});
        commands.add(text(0, 0, "XY"));
    } else if (test == "empty_clip") {
        commands.add(PushClipCommand{Rect(30, 0, -20, 80)});
        commands.add(text(0, 20, "ABCDE"));
        commands.add(border(Rect(0, 0, 50, 80)));
        commands.add(shadow(Rect(0, 0, 50, 80), 0, 0, Color::red()));
        commands.add(PopClipCommand{});
        commands.add(text(0, 0, "OK"));
    } else if (test == "text_negative") {
        commands.add(text(-5, 0, "ABCDE"));
        commands.add(text(0, -5, "WRONG"));
    } else if (test == "text_scrolled") {
        terminal.set_scroll(15, 20);
        commands.add(text(0, 20, "ABCDE"));
    } else if (test == "transparent_text") {
        commands.add(text(0, 0, "GHOST", Color::transparent()));
        commands.add(text(0, 20, "RED", Color(255, 0, 0, 128)));
    } else if (test == "shadow_offset") {
        commands.add(shadow(Rect(10, 20, 20, 20), 10, 0, Color(255, 0, 0, 128)));
    } else if (test == "shadow_clipped") {
        commands.add(PushClipCommand{Rect(30, 20, 10, 20)});
        commands.add(shadow(Rect(10, 20, 20, 20), 10, 0, Color(255, 0, 0, 128)));
        commands.add(PopClipCommand{});
    } else if (test == "shadow_offscreen") {
        commands.add(shadow(Rect(-50, -40, 20, 20), 0, 0, Color::red()));
    } else if (test == "rect_clipped_control") {
        commands.add(PushClipCommand{Rect(10, 20, 20, 20)});
        commands.add(DrawRectCommand{Rect(0, 0, 50, 80), Color::red()});
        commands.add(PopClipCommand{});
    } else if (test == "image_scroll_control") {
        terminal.set_scroll(10, 0);
        auto image = std::make_shared<ImageData>();
        image->width = 20;
        image->height = 20;
        for (int y = 0; y < 20; ++y) {
            for (int x = 0; x < 20; ++x) {
                image->pixels.push_back((x < 10 ? Color::red() : Color(0, 255, 0)).to_argb());
            }
        }
        commands.add(DrawImageCommand{Rect(0, 0, 20, 20), image});
    } else if (test == "focus_exact") {
        terminal.draw_focus_ring(Rect(0, 0, 20, 40));
    } else if (test == "focus_partial") {
        terminal.draw_focus_ring(Rect(-10, -20, 40, 60));
    } else if (test == "focus_offscreen") {
        terminal.draw_focus_ring(Rect(0, -80, 50, 20));
    } else if (test == "invalid_geometry") {
        const float inf = std::numeric_limits<float>::infinity();
        const float nan = std::numeric_limits<float>::quiet_NaN();
        commands.add(border(Rect(inf, 0, 20, 20)));
        commands.add(border(Rect(0, 0, 0, 20)));
        commands.add(border(Rect(20, 0, -20, 20)));
        commands.add(shadow(Rect(nan, 0, 20, 20), 0, 0, Color::red()));
        commands.add(shadow(Rect(20, 0, -20, 20), 0, 0, Color::red()));
        commands.add(text(inf, 0, "WRONG"));
        commands.add(text(0, nan, "WRONG"));
        auto invalid_transform = TransformMatrix2D::identity();
        invalid_transform.a = nan;
        commands.add(PushTransformCommand{invalid_transform});
        commands.add(border(Rect(0, 0, 30, 60)));
        commands.add(shadow(Rect(0, 0, 30, 60), 0, 0, Color::red()));
        commands.add(text(0, 0, "Z"));
        commands.add(PushClipCommand{Rect(0, 0, 50, 80)});
        commands.add(PopTransformCommand{});
        commands.add(text(0, 0, "NO")); // the invalid clip stays empty after pop
        commands.add(PopClipCommand{});
        terminal.draw_focus_ring(Rect(0, 0, 0, 20));
    } else if (test == "unicode_latin") {
        // "café résumé": every é is one character in one cell, not two bytes
        // in two cells.
        DisplayList commands;
        commands.add(text(0, 0, "caf\xC3\xA9 r\xC3\xA9sum\xC3\xA9"));
        TerminalRenderer terminal(11, 4);
        std::cout << terminal.to_ansi_string(commands);
        return 0;
    } else if (test == "unicode_wide") {
        // 日本語 is three characters taking two columns each, so only two of
        // them fit in a five-column viewport and both take two cells.
        DisplayList commands;
        commands.add(text(0, 0, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E"));
        TerminalRenderer terminal(5, 4);
        std::cout << terminal.to_ansi_string(commands);
        return 0;
    } else if (test == "unicode_combining") {
        // "e" + U+0301 is one grapheme cluster and one cell (UAX #29).
        DisplayList commands;
        commands.add(text(0, 0, "e\xCC\x81" "X"));  // two literals: \x81X would be one escape
        TerminalRenderer terminal(5, 4);
        std::cout << terminal.to_ansi_string(commands);
        return 0;
    } else if (test == "unicode_control") {
        // ESC, DEL and the C1 CSI become U+FFFD: they are never written to the
        // stream, but a cell still appears where the page asked for one.
        DisplayList commands;
        commands.add(text(0, 0, "A\x1b" "B\x7f" "C\xC2\x9b" "D"));
        TerminalRenderer terminal(5, 4);
        std::cout << terminal.to_ansi_string(commands);
        return 0;
    } else if (test == "unicode_malformed") {
        // A stray continuation byte and a truncated sequence are each one
        // replacement character, and the walk does not run past either.
        DisplayList commands;
        commands.add(text(0, 0, "A\x80" "B\xE6\x97"));  // the last two bytes are truncated
        TerminalRenderer terminal(5, 4);
        std::cout << terminal.to_ansi_string(commands);
        return 0;
    } else {
        std::cerr << "unknown case: " << test << '\n';
        return 2;
    }
    std::cout << terminal.to_ansi_string(commands);
}
