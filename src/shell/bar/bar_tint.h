#pragma once

#include "render/core/color.h"

#include <cstddef>
#include <cstdint>
#include <span>

// Immersive bars: derive the bar's surface, opacity, and foreground from the
// screen content sitting next to it, so the bar reads as part of the window it
// touches instead of a fixed theme surface. Everything here is pure — the
// screencopy capture that feeds reduce() lives in Bar.
namespace noctalia::bar::tint {

  // Statistics reduced from one strip of screen content.
  struct Sample {
    // Representative color of the strip, after trimming the luminance extremes a
    // window's chrome (title bar, cursor, scrollbar) contributes.
    Color color{};
    // Hue in turns ([0,1)) and saturation of `color`.
    float hueTurns = 0.0F;
    float saturation = 0.0F;
    // WCAG relative luminance of `color`, in [0,1].
    float luminance = 0.0F;
    // Standard deviation of per-pixel luminance, in [0,1]. Rises with how much
    // detail the strip carries; drives how opaque the bar has to be.
    float spread = 0.0F;
    bool valid = false;
  };

  // Reduces a wl_shm RGBA strip (premultiplied, as wl_shm delivers it) to a
  // single representative color plus the statistics derive() needs.
  [[nodiscard]] Sample reduce(std::span<const std::uint8_t> rgba, int width, int height);

  struct Style {
    Color surface{};      // bar background fill
    float opacity = 1.0F; // bar background alpha
    // `surface` composited over the sampled content — what the bar's text and
    // icons actually sit on once the alpha is applied.
    Color backdrop{};
    Color foreground{}; // text and icons
    bool valid = false;
  };

  // Derives the bar style from a sample. `onSurface` is the palette's OnSurface
  // role: the foreground keeps that color's hue and chroma and only travels in
  // lightness until it clears WCAG 4.5:1 against the backdrop. That is what
  // makes the bar readable on any content without the mud of a true color
  // inversion, which would drag a saturated blue backdrop to orange.
  [[nodiscard]] Style derive(const Sample& sample, const Color& onSurface, bool blurred);

  // Pulls a semantic accent (battery warning, mute, error) back to legibility on
  // the immersive backdrop. Uses WCAG's 3:1 floor for non-text UI components,
  // so warnings stay recognizable instead of being flattened to the foreground.
  [[nodiscard]] Color adjustAccent(const Color& accent, const Style& style);

} // namespace noctalia::bar::tint
