#pragma once

#include "litebird/common/Geometry.hpp"
#include <string>
#include <vector>

namespace litebird {

/// §4.12.5.1.7's `Path2D`: a path a page holds a reference to, separate from the
/// context's current path.
///
/// ## Why it records calls instead of holding points
///
/// The context's own path (`m_paths`) holds **device-space** points: a
/// `lineTo` maps the argument through the CTM at the moment of the call. A
/// `Path2D` cannot do that. The specification stores the path in user space and
/// applies the CTM when it is *used*, which is why
///
/// ```js
/// const p = new Path2D();
/// p.rect(0, 0, 10, 10);       // under whatever transform
/// ctx.setTransform(2, 0, 0, 2, 0, 0);
/// ctx.fill(p);                 // drawn at twice the size
/// ```
///
/// draws a 20×20 box. If `Path2D` stored device points it would draw 10×10, and
/// a page that built a path at one size and drew it at another would get the
/// first size — the same class of quiet wrongness finding 417 was about.
///
/// So a `Path2D` keeps the **calls** it was given: `moveTo`, `lineTo`,
/// `bezierCurveTo`, `arc`, and the rest, in order. Replaying them into a context
/// runs the context's own code — the same flattening, the same arc-to-bezier, the
/// same CTM mapping — so a `Path2D` cannot disagree with `moveTo` + `lineTo` about
/// how a curve becomes pixels. That is the whole design and it is why this file is
/// small.
///
/// The cost is that a path is re-flattened every time it is drawn, where a
/// device-space path would be flattened once. Correctness first; a cache of the
/// flattened form would have to be per-CTM, which is the same work again.
class Path2D {
public:
    /// One recorded call. `kind` says which; the arguments are the call's own,
    /// all in the user space that was in force when the page made it.
    enum class Kind {
        MoveTo,
        LineTo,
        QuadraticCurveTo,
        BezierCurveTo,
        ArcTo,
        Arc,
        Ellipse,
        Rect,
        RoundRect,
        ClosePath,
        /// `addPath(path, transform)`: another path, and the matrix to apply to
        /// it. Kept as a pointer so a path added twice is the same path.
        AddPath,
    };

    struct Op {
        Kind kind{Kind::ClosePath};
        float a{0.0f}, b{0.0f}, c{0.0f}, d{0.0f}, e{0.0f}, f{0.0f}, g{0.0f}, h{0.0f};
        bool clockwise{false};
        std::string radii;  // `roundRect`'s radii, still as the page wrote them
        std::shared_ptr<const Path2D> path;
        TransformMatrix2D transform{TransformMatrix2D::identity()};
    };

    Path2D() = default;

    /// `new Path2D(string)` — SVG path data, or `new Path2D(otherPath)`, a copy
    /// of what that path holds at the moment of the copy. The copy is of the
    /// recorded calls, so adding to the original afterwards changes nothing here,
    /// which is what the specification says.
    explicit Path2D(const Path2D& other) : m_ops(other.m_ops) {}
    /// Nothing to free; declared so the recording type is a real class.
    ~Path2D() = default;

    /// The recorded calls, in order. Replayed by `replay`.
    [[nodiscard]] const std::vector<Op>& ops() const { return m_ops; }
    void add_op(Op op) { m_ops.push_back(std::move(op)); }

    /// True when nothing has been recorded: an empty path fills nothing, and
    /// `stroke`/`fill` with it is a no-op rather than an error.
    [[nodiscard]] bool empty() const { return m_ops.empty(); }

    /// Number of recorded calls. A cap on this is what stops a page from making a
    /// path large enough to exhaust memory one `lineTo` at a time.
    [[nodiscard]] std::size_t size() const { return m_ops.size(); }

private:
    std::vector<Op> m_ops;
};

} // namespace litebird
