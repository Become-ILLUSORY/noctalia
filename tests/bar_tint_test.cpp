#include "render/core/color.h"
#include "shell/bar/bar_tint.h"
#include "theme/contrast.h"
#include "ui/palette.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

  bool expect(bool condition, const char* message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
  }

  // The tint module works in render::Color (floats); the contrast helpers work in
  // theme::Color (bytes). These bridge the two so the assertions read clearly.
  noctalia::theme::Color toTheme(const Color& color) {
    return noctalia::theme::Color(
        static_cast<int>(std::lround(color.r * 255.0F)), static_cast<int>(std::lround(color.g * 255.0F)),
        static_cast<int>(std::lround(color.b * 255.0F))
    );
  }

  double contrastAgainst(const Color& foreground, const Color& background) {
    return noctalia::theme::contrastRatio(toTheme(foreground), toTheme(background));
  }

  double luminanceOf(const Color& color) {
    const auto theme = toTheme(color);
    return noctalia::theme::relativeLuminance(theme.r, theme.g, theme.b);
  }

  // Builds an opaque RGBA strip filled with one color, the shape a window body
  // presents to the sampler.
  std::vector<std::uint8_t> solidStrip(std::uint8_t r, std::uint8_t g, std::uint8_t b, int width = 8, int height = 4) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U);
    for (std::size_t i = 0; i < pixels.size(); i += 4U) {
      pixels[i] = r;
      pixels[i + 1] = g;
      pixels[i + 2] = b;
      pixels[i + 3] = 255;
    }
    return pixels;
  }

  bool reducesSolidColor() {
    bool ok = true;
    const auto strip = solidStrip(20, 40, 200);
    const auto sample = noctalia::bar::tint::reduce(strip, 8, 4);
    ok = expect(sample.valid, "solid strip should produce a valid sample") && ok;
    if (!sample.valid) {
      return false;
    }
    ok = expect(std::fabs(sample.color.r - 20.0F / 255.0F) < 0.02F, "red channel should survive reduction") && ok;
    ok = expect(std::fabs(sample.color.g - 40.0F / 255.0F) < 0.02F, "green channel should survive reduction") && ok;
    ok = expect(std::fabs(sample.color.b - 200.0F / 255.0F) < 0.02F, "blue channel should survive reduction") && ok;
    // A flat strip has no detail, so the spread that drives bar opacity stays low.
    ok = expect(sample.spread < 0.02F, "uniform strip should report near-zero spread") && ok;
    return ok;
  }

  bool rejectsMalformedBuffers() {
    bool ok = true;
    const std::vector<std::uint8_t> shortBuffer(3, 0);
    ok = expect(!noctalia::bar::tint::reduce(shortBuffer, 4, 4).valid, "undersized buffer should be rejected") && ok;
    ok = expect(!noctalia::bar::tint::reduce(shortBuffer, 0, 0).valid, "empty geometry should be rejected") && ok;
    return ok;
  }

  bool trimsLuminanceExtremes() {
    bool ok = true;
    // A dark body with a thin bright strip along the top: the shape a title bar
    // or a bright scrollbar takes. The representative color must follow the body.
    const int width = 20;
    const int height = 20;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0);
    for (int y = 0; y < height; ++y) {
      const bool brightRow = y < 2;
      for (int x = 0; x < width; ++x) {
        auto* pixel = pixels.data()
            + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
        pixel[0] = brightRow ? 250 : 12;
        pixel[1] = brightRow ? 250 : 12;
        pixel[2] = brightRow ? 250 : 12;
        pixel[3] = 255;
      }
    }
    const auto sample = noctalia::bar::tint::reduce(pixels, width, height);
    ok = expect(sample.valid, "mixed strip should produce a valid sample") && ok;
    if (sample.valid) {
      ok = expect(sample.luminance < 0.2F, "representative color should follow the body, not the bright sliver") && ok;
    }
    return ok;
  }

  bool straightensPremultipliedAlpha() {
    bool ok = true;
    // Half-transparent white premultiplied is (128,128,128,128) on the wire; its
    // straight color is white, and treating it as grey would darken the bar.
    std::vector<std::uint8_t> pixels(4, 128);
    pixels[3] = 128;
    const auto sample = noctalia::bar::tint::reduce(pixels, 1, 1);
    ok = expect(sample.valid, "premultiplied strip should produce a valid sample") && ok;
    if (sample.valid) {
      ok = expect(sample.color.r > 0.9F, "premultiplied channels should be straightened before averaging") && ok;
    }
    return ok;
  }

  // The core readability guarantee: whatever the backdrop, the derived foreground
  // has to clear the WCAG AA ratio against what the bar's ink actually lands on.
  bool foregroundStaysReadable() {
    bool ok = true;
    const std::array<std::array<std::uint8_t, 3>, 8> backdrops{{
        {255, 255, 255},
        {0, 0, 0},
        {20, 40, 200},
        {250, 240, 120},
        {10, 120, 40},
        {200, 40, 90},
        {128, 128, 128},
        {30, 30, 30},
    }};

    for (const auto& backdrop : backdrops) {
      const auto strip = solidStrip(backdrop[0], backdrop[1], backdrop[2]);
      const auto sample = noctalia::bar::tint::reduce(strip, 8, 4);
      const auto style = noctalia::bar::tint::derive(sample, rgba(0.9F, 0.9F, 0.92F), false);
      if (!expect(style.valid, "derive should accept a valid sample")) {
        ok = false;
        continue;
      }
      const auto ratio = contrastAgainst(style.foreground, style.backdrop);
      ok = expect(ratio >= 4.4, "derived foreground must clear WCAG AA against the backdrop") && ok;
    }
    return ok;
  }

  // A true inversion would turn a saturated blue backdrop orange. The foreground
  // has to keep the palette's hue while only travelling in lightness.
  bool keepsPaletteHueRatherThanInverting() {
    bool ok = true;
    const auto strip = solidStrip(20, 40, 200);
    const auto sample = noctalia::bar::tint::reduce(strip, 8, 4);

    const Color bluish = rgba(0.45F, 0.55F, 0.95F);
    const auto style = noctalia::bar::tint::derive(sample, bluish, false);
    ok = expect(style.valid, "derive should accept a saturated backdrop") && ok;
    if (!style.valid) {
      return false;
    }
    ok = expect(style.foreground.b >= style.foreground.r, "foreground should keep the palette's cool cast") && ok;

    // And the derived surface must not be the complement of the content.
    const float invertedRed = 1.0F - sample.color.r;
    ok = expect(
             std::fabs(style.surface.r - invertedRed) > 0.15F,
             "surface should not be a channel inversion of the sampled content"
         )
        && ok;
    return ok;
  }

  bool surfaceFollowsContentPolarity() {
    bool ok = true;
    const auto darkStrip = solidStrip(12, 12, 16);
    const auto lightStrip = solidStrip(248, 248, 250);
    const Color onSurface = rgba(0.9F, 0.9F, 0.92F);

    const auto darkStyle = noctalia::bar::tint::derive(noctalia::bar::tint::reduce(darkStrip, 8, 4), onSurface, false);
    const auto lightStyle =
        noctalia::bar::tint::derive(noctalia::bar::tint::reduce(lightStrip, 8, 4), onSurface, false);

    ok = expect(darkStyle.valid && lightStyle.valid, "both strips should derive") && ok;
    if (!darkStyle.valid || !lightStyle.valid) {
      return false;
    }
    ok = expect(luminanceOf(darkStyle.surface) < 0.05, "dark content should yield a dark bar") && ok;
    ok = expect(luminanceOf(lightStyle.surface) > 0.6, "light content should yield a light bar") && ok;
    return ok;
  }

  // Busy content needs a more opaque bar, and the opacity must stay inside a
  // band that is still recognizably a tinted bar rather than a solid slab.
  bool opacityRespondsToDetail() {
    bool ok = true;
    const int width = 16;
    const int height = 8;
    std::vector<std::uint8_t> noisy(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U, 0);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        auto* pixel = noisy.data()
            + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
        const auto value = static_cast<std::uint8_t>(((x + y) % 2) == 0 ? 24 : 232);
        pixel[0] = value;
        pixel[1] = value;
        pixel[2] = value;
        pixel[3] = 255;
      }
    }

    const Color onSurface = rgba(0.9F, 0.9F, 0.92F);
    const auto flat = noctalia::bar::tint::derive(
        noctalia::bar::tint::reduce(solidStrip(30, 30, 34, width, height), width, height), onSurface, false
    );
    const auto busy = noctalia::bar::tint::derive(noctalia::bar::tint::reduce(noisy, width, height), onSurface, false);

    ok = expect(flat.valid && busy.valid, "both strips should derive") && ok;
    if (!flat.valid || !busy.valid) {
      return false;
    }
    ok = expect(busy.opacity > flat.opacity, "detailed content should raise bar opacity") && ok;
    ok = expect(flat.opacity >= 0.5F && flat.opacity <= 1.0F, "opacity should stay in its band") && ok;
    ok = expect(busy.opacity <= 1.0F, "opacity should never exceed 1") && ok;
    return ok;
  }

  bool accentsStayLegible() {
    bool ok = true;
    const auto lightStrip = solidStrip(250, 250, 250);
    const auto style =
        noctalia::bar::tint::derive(noctalia::bar::tint::reduce(lightStrip, 8, 4), rgba(0.9F, 0.9F, 0.92F), false);
    if (!expect(style.valid, "light strip should derive")) {
      return false;
    }

    // A mid amber reads well on dark bars and disappears on a light one; the
    // accent path has to pull it back without discarding it entirely.
    const Color amber = rgba(0.85F, 0.65F, 0.1F);
    const Color adjusted = noctalia::bar::tint::adjustAccent(amber, style);
    const auto ratio = contrastAgainst(adjusted, style.backdrop);
    ok = expect(ratio >= 2.9, "adjusted accent must clear the 3:1 non-text contrast floor") && ok;
    ok = expect(adjusted.r >= adjusted.b, "adjusted accent should stay warm rather than inverting hue") && ok;
    return ok;
  }

  bool invalidSampleLeavesStyleUnset() {
    noctalia::bar::tint::Sample empty;
    const auto style = noctalia::bar::tint::derive(empty, rgba(0.9F, 0.9F, 0.92F), false);
    return expect(!style.valid, "an invalid sample should not produce a style");
  }

} // namespace

int main() {
  bool ok = true;
  ok = reducesSolidColor() && ok;
  ok = rejectsMalformedBuffers() && ok;
  ok = trimsLuminanceExtremes() && ok;
  ok = straightensPremultipliedAlpha() && ok;
  ok = foregroundStaysReadable() && ok;
  ok = keepsPaletteHueRatherThanInverting() && ok;
  ok = surfaceFollowsContentPolarity() && ok;
  ok = opacityRespondsToDetail() && ok;
  ok = accentsStayLegible() && ok;
  ok = invalidSampleLeavesStyleUnset() && ok;
  return ok ? 0 : 1;
}
