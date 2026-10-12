#pragma once

#include "compositors/workspace_backend.h"

#include <cstdint>
#include <vector>

// Whether a window abuts a bar's inner edge.
//
// Extracted from CompositorPlatform so the rule can be tested without a Wayland
// connection: it is the decision that keeps an immersive bar from tinting itself
// with the wallpaper behind a floating window, and it is easy to get subtly
// wrong (an off-by-one in the tolerance, or counting a window that reports no
// geometry at all).
namespace compositors {

  // Tolerance in logical pixels. A tiling compositor can place a window one or
  // two pixels off the bar's inner edge through fractional-scale rounding, and
  // that window still abuts the bar visually.
  inline constexpr std::int32_t kAbutTolerancePx = 2;

  // Templated so it serves both WorkspaceWindow and WorkspaceWindowAssignment,
  // which carry the same geometry under different names.
  //
  // `innerEdge` is the bar's inner edge in output-local logical pixels, along the
  // bar's cross axis (y for a horizontal bar, x for a vertical one). `vertical`
  // selects which window coordinate to compare.
  template <typename Window>
  [[nodiscard]] bool windowAbutsEdge(const Window& window, std::int32_t innerEdge, bool vertical) noexcept {
    // A backend that cannot report extents leaves these at zero. Treating that
    // as "at the origin, therefore abutting" would light up immersive for every
    // window on a compositor with no geometry support, so unknown is skipped.
    if (window.width <= 0 || window.height <= 0) {
      return false;
    }

    const std::int32_t nearEdge = vertical ? window.x : window.y;
    const std::int32_t extent = vertical ? window.width : window.height;
    const std::int32_t farEdge = nearEdge + extent;

    // Touching means the bar's inner edge lies within the window's span, or just
    // outside it by no more than the tolerance. A window starting above the edge
    // (a maximized window behind a floating bar) covers it and counts; one
    // starting below it by more than the tolerance leaves a gap and does not.
    return innerEdge >= nearEdge - kAbutTolerancePx && innerEdge <= farEdge + kAbutTolerancePx;
  }

  template <typename Window>
  [[nodiscard]] bool
  anyWindowAbutsEdge(const std::vector<Window>& windows, std::int32_t innerEdge, bool vertical) noexcept {
    for (const auto& window : windows) {
      if (windowAbutsEdge(window, innerEdge, vertical)) {
        return true;
      }
    }
    return false;
  }

} // namespace compositors
