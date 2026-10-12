// The immersive bar samples the screen content it abuts, so deciding *whether* a
// window abuts it is what keeps the bar from tinting itself with the wallpaper
// behind a floating window. These cover the rule's edges: tolerance, unknown
// geometry, and both bar orientations.

#include "compositors/window_abut.h"
#include "tests/test_check.h"

#include <cstdint>
#include <vector>

namespace {

  using compositors::anyWindowAbutsEdge;
  using compositors::windowAbutsEdge;

  WorkspaceWindow windowAt(std::int32_t x, std::int32_t y, std::int32_t width, std::int32_t height) {
    WorkspaceWindow window;
    window.x = x;
    window.y = y;
    window.width = width;
    window.height = height;
    return window;
  }

  void abuttingWindowCounts() {
    // A top bar whose inner edge is y=44; the window starts exactly there.
    const auto window = windowAt(0, 44, 1920, 1036);
    TEST_CHECK(windowAbutsEdge(window, 44, false));
  }

  void gappedWindowDoesNotCount() {
    // The case from the bug report: a floating window with a visible gap below
    // the bar must not trigger immersive.
    const auto window = windowAt(0, 60, 1200, 800);
    TEST_CHECK(!windowAbutsEdge(window, 44, false));
  }

  void toleranceAbsorbsRounding() {
    // Fractional scale can place a tiling window a pixel or two off the edge.
    TEST_CHECK(windowAbutsEdge(windowAt(0, 46, 1920, 1000), 44, false));
    TEST_CHECK(windowAbutsEdge(windowAt(0, 42, 1920, 1000), 44, false));
    // Beyond the tolerance it is a gap, not rounding.
    TEST_CHECK(!windowAbutsEdge(windowAt(0, 47, 1920, 1000), 44, false));
  }

  void windowAboveTheEdgeStillCounts() {
    // A window can begin above the bar's inner edge when the bar floats over it;
    // it is still touching.
    const auto window = windowAt(0, 0, 1920, 1080);
    TEST_CHECK(windowAbutsEdge(window, 44, false));
  }

  void unknownGeometryIsSkipped() {
    // A backend that reports no extents leaves these zero. Counting that as
    // "at the origin" would enable immersive for every window.
    TEST_CHECK(!windowAbutsEdge(windowAt(0, 44, 0, 0), 44, false));
    TEST_CHECK(!windowAbutsEdge(windowAt(0, 44, 1920, 0), 44, false));
    TEST_CHECK(!windowAbutsEdge(windowAt(0, 44, 0, 1000), 44, false));
  }

  void verticalBarUsesTheXAxis() {
    // Left bar with its inner edge at x=44.
    TEST_CHECK(windowAbutsEdge(windowAt(44, 0, 1876, 1080), 44, true));
    TEST_CHECK(!windowAbutsEdge(windowAt(60, 0, 1800, 1080), 44, true));
  }

  void anyWindowWins() {
    std::vector<WorkspaceWindow> windows{
        windowAt(0, 300, 800, 600),  // floating, far from the bar
        windowAt(0, 44, 1920, 1036), // tiled against it
    };
    TEST_CHECK(anyWindowAbutsEdge(windows, 44, false));

    std::vector<WorkspaceWindow> gapped{
        windowAt(0, 300, 800, 600),
        windowAt(0, 120, 1920, 900),
    };
    TEST_CHECK(!anyWindowAbutsEdge(gapped, 44, false));
    TEST_CHECK(!anyWindowAbutsEdge(std::vector<WorkspaceWindow>{}, 44, false));
  }

} // namespace

int main() {
  abuttingWindowCounts();
  gappedWindowDoesNotCount();
  toleranceAbsorbsRounding();
  windowAboveTheEdgeStillCounts();
  unknownGeometryIsSkipped();
  verticalBarUsesTheXAxis();
  anyWindowWins();
  return 0;
}
