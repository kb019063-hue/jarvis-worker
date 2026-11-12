#pragma once

// How much pixel memory a page may make the engine allocate.
//
// A <canvas> and an <svg> are each drawn into a bitmap of their own, as large
// as the element says it is. Nothing bounded that. One line of markup,
// <svg width="100000" height="100000">, asked for 40 GB and the process ended
// with std::bad_alloc; width="1e9" ended it with std::length_error before any
// memory was requested at all; and twenty thousand <svg> elements of the
// default size, 180 KB each, asked for 3.6 GB a little at a time.
//
// Two limits, both in pixels (four bytes each):
//
//   * one bitmap: at most kMaxSide on a side and kMaxPixels in all;
//   * all bitmaps of canvases and SVG images alive at one time: kMaxLivePixels.
//
// An element over a limit keeps its size in layout and is left blank: a
// canvas gets a backing store of a single pixel, on which drawing does
// nothing, and an <svg> gets no image. That is what browsers do with a canvas
// that is too large for them. Nothing throws, and nothing depends on how much
// memory the machine has.
//
// Images decoded from files are limited separately, per image
// (kMaxDecodedPixels in ImageDecoder.cpp), and do not count here.

#include <atomic>
#include <cstdint>

namespace litebird {

class RasterBudget {
public:
    /// The longest side of one bitmap.
    static constexpr int kMaxSide = 16384;
    /// The most pixels in one bitmap: 4096 x 4096, 64 MB.
    static constexpr std::int64_t kMaxPixels = 16ll * 1024 * 1024;
    /// The most pixels in all live bitmaps together: 256 MB.
    static constexpr std::int64_t kMaxLivePixels = 64ll * 1024 * 1024;

    /// True when a bitmap of this size is within the limit for one bitmap.
    /// Written with a division, so that no product can overflow.
    static bool allows(std::int64_t width, std::int64_t height) {
        if (width <= 0 || height <= 0) return false;
        if (width > kMaxSide || height > kMaxSide) return false;
        return width <= kMaxPixels / height;
    }

    /// Pixels currently reserved.
    static std::int64_t live_pixels() { return counter().load(std::memory_order_relaxed); }

private:
    friend class RasterReservation;
    static std::atomic<std::int64_t>& counter() {
        static std::atomic<std::int64_t> live{0};
        return live;
    }
};

/// A share of the budget, held by one bitmap and given back when the bitmap
/// goes away. A copy of a bitmap starts with no share: only the canvas and
/// the SVG renderer reserve, for the bitmaps they create.
class RasterReservation {
public:
    RasterReservation() = default;
    RasterReservation(const RasterReservation&) {}
    RasterReservation& operator=(const RasterReservation&) { return *this; }
    ~RasterReservation() { resize(0); }

    /// Changes this share to `pixels`. Returns false, and keeps the share it
    /// had, when the budget does not have that much left.
    bool resize(std::int64_t pixels) {
        if (pixels < 0) pixels = 0;
        auto& live = RasterBudget::counter();
        const std::int64_t change = pixels - m_pixels;
        if (change <= 0) {
            live.fetch_add(change, std::memory_order_relaxed);
            m_pixels = pixels;
            return true;
        }
        // Canvases are created on worker threads too.
        std::int64_t now = live.load(std::memory_order_relaxed);
        do {
            if (now + change > RasterBudget::kMaxLivePixels) return false;
        } while (!live.compare_exchange_weak(now, now + change, std::memory_order_relaxed));
        m_pixels = pixels;
        return true;
    }

    std::int64_t pixels() const { return m_pixels; }

private:
    std::int64_t m_pixels{0};
};

} // namespace litebird
