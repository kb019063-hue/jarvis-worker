#pragma once

#include "litebird/js/Heap.hpp"

#include <cstdint>
#include <vector>

namespace litebird {

/// The host state behind an `ImageBitmap`: the pixels a script can draw into a
/// canvas, and the two flags the standard attaches to them.
///
/// It lives in a header because two translation units need it: the one that
/// makes bitmaps (`createImageBitmap`, in DomWindowBindings.cpp) and the one
/// that draws them (`drawImage`, in DomHtmlBindings.cpp). Which pixels a
/// bitmap may hand to a canvas — and whether drawing it taints that canvas —
/// is decided by `origin_clean`, so the two sides must agree on the field
/// rather than each keeping their own idea of it.
struct ImageBitmapState : JsExotic {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;
    bool closed = false;
    /// HTML §4.12.5.1: an ImageBitmap built from a source that is not
    /// CORS-same-origin carries that on, and drawing it clears the destination
    /// canvas's origin-clean flag. A bitmap made from a canvas is as clean as
    /// that canvas was, because copying its pixels copies its origin too.
    bool origin_clean = true;
};

} // namespace litebird
