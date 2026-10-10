#include "shell/bar/bar_tint.h"

#include "theme/contrast.h"
#include "ui/palette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

  // Fraction of pixels dropped from each end of the luminance histogram before
  // averaging. A window's title bar, cursor, or scrollbar is a thin bright or
  // dark sliver against the body; trimming it keeps the representative color on
  // the content the bar actually sits against.
  constexpr float kTrimFraction = 0.05F;

  constexpr std::size_t kLuminanceBins = 256;

  // Surface lightness bands. A window that is dark yields a dark bar and a light
  // window a light one, mirroring how a phone status bar follows the app behind
  // it, while the band keeps the result reading as a surface rather than as a
  // photo average that can land on an unusable mid grey.
  constexpr float kDarkBandLow = 0.10F;
  constexpr float kDarkBandHigh = 0.24F;
  constexpr float kLightBandLow = 0.88F;
  constexpr float kLightBandHigh = 0.96F;

  // Chroma caps: saturated surfaces look like a bug at bar thickness, and the
  // lighter band tolerates less of it before turning neon.
  constexpr float kDarkBandMaxSaturation = 0.32F;
  constexpr float kLightBandMaxSaturation = 0.20F;

  // Blending the sampled color toward the band is what keeps the bar from
  // tracking content luminance one-to-one; a little anchoring keeps windows of
  // the same hue family from producing visibly different bars.
  constexpr float kContentLightnessWeight = 0.55F;

  constexpr float kTextContrast = 4.5F;   // WCAG AA for body text
  constexpr float kAccentContrast = 3.0F; // WCAG AA for non-text UI components

  [[nodiscard]] float channelByte(std::uint8_t value) { return static_cast<float>(value) / 255.0F; }

  [[nodiscard]] noctalia::theme::Color toThemeColor(const Color& color) {
    const auto toByte = [](float channel) {
      return static_cast<int>(std::lround(std::clamp(channel, 0.0F, 1.0F) * 255.0F));
    };
    return noctalia::theme::Color(toByte(color.r), toByte(color.g), toByte(color.b));
  }

  [[nodiscard]] Color fromThemeColor(const noctalia::theme::Color& color) {
    const auto toFloat = [](int channel) { return static_cast<float>(std::clamp(channel, 0, 255)) / 255.0F; };
    return rgba(toFloat(color.r), toFloat(color.g), toFloat(color.b));
  }

  // Rec. 709 coefficients on linearized channels, matching render::relativeLuminance.
  [[nodiscard]] float luminanceOf(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return relativeLuminance(rgba(channelByte(r), channelByte(g), channelByte(b)));
  }

  // wl_shm hands back premultiplied alpha; straighten it so a translucent edge
  // does not read as a darker color than it is. Desktop content is opaque in
  // practice, so this is a guard rather than the common path.
  [[nodiscard]] std::uint8_t straighten(std::uint8_t channel, std::uint8_t alpha) {
    if (alpha == 0) {
      return 0;
    }
    if (alpha == 255) {
      return channel;
    }
    const auto scaled = (static_cast<std::uint32_t>(channel) * 255U + static_cast<std::uint32_t>(alpha) / 2U)
        / static_cast<std::uint32_t>(alpha);
    return static_cast<std::uint8_t>(std::min<std::uint32_t>(scaled, 255U));
  }

  // Maps a content luminance onto the surface band for its polarity, then pulls
  // it part of the way toward the content so windows within a band stay
  // distinguishable without the bar swinging across the full range.
  [[nodiscard]] float surfaceLightness(float contentLuminance, bool contentIsDark) {
    if (contentIsDark) {
      const float position = std::clamp(contentLuminance / 0.5F, 0.0F, 1.0F);
      const float banded = kDarkBandLow + position * (kDarkBandHigh - kDarkBandLow);
      return std::clamp(banded + (contentLuminance - banded) * kContentLightnessWeight, kDarkBandLow, kDarkBandHigh);
    }
    const float position = std::clamp((contentLuminance - 0.5F) / 0.5F, 0.0F, 1.0F);
    const float banded = kLightBandLow + position * (kLightBandHigh - kLightBandLow);
    return std::clamp(banded + (contentLuminance - banded) * kContentLightnessWeight, kLightBandLow, kLightBandHigh);
  }

} // namespace

namespace noctalia::bar::tint {

  Sample reduce(std::span<const std::uint8_t> pixels, int width, int height) {
    Sample sample;
    if (width <= 0 || height <= 0) {
      return sample;
    }
    const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels.size() < pixelCount * 4U) {
      return sample;
    }

    std::array<std::uint32_t, kLuminanceBins> histogram{};
    for (std::size_t i = 0; i < pixelCount; ++i) {
      const auto* pixel = pixels.data() + i * 4U;
      const std::uint8_t alpha = pixel[3];
      const float luminance =
          luminanceOf(straighten(pixel[0], alpha), straighten(pixel[1], alpha), straighten(pixel[2], alpha));
      const auto bin = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(luminance * 255.0F)), 0, 255));
      ++histogram[bin];
    }

    const auto trim = static_cast<std::uint32_t>(std::lround(static_cast<float>(pixelCount) * kTrimFraction));
    std::size_t lowBin = 0;
    std::size_t highBin = kLuminanceBins - 1;
    std::uint32_t dropped = 0;
    for (; lowBin < kLuminanceBins - 1; ++lowBin) {
      if (dropped + histogram[lowBin] > trim) {
        break;
      }
      dropped += histogram[lowBin];
    }
    dropped = 0;
    for (; highBin > lowBin; --highBin) {
      if (dropped + histogram[highBin] > trim) {
        break;
      }
      dropped += histogram[highBin];
    }

    std::uint64_t sumR = 0;
    std::uint64_t sumG = 0;
    std::uint64_t sumB = 0;
    double sumLuminance = 0.0;
    double sumLuminanceSquared = 0.0;
    std::size_t included = 0;
    for (std::size_t i = 0; i < pixelCount; ++i) {
      const auto* pixel = pixels.data() + i * 4U;
      const std::uint8_t alpha = pixel[3];
      const std::uint8_t r = straighten(pixel[0], alpha);
      const std::uint8_t g = straighten(pixel[1], alpha);
      const std::uint8_t b = straighten(pixel[2], alpha);
      const float luminance = luminanceOf(r, g, b);
      // Compare bins rather than luminance: the cut points come from the same
      // quantization, so a pixel on a bin boundary cannot fall outside its own
      // bin and get dropped.
      const auto bin = static_cast<std::size_t>(std::clamp(static_cast<int>(std::lround(luminance * 255.0F)), 0, 255));
      if (bin < lowBin || bin > highBin) {
        continue;
      }
      sumR += r;
      sumG += g;
      sumB += b;
      sumLuminance += static_cast<double>(luminance);
      sumLuminanceSquared += static_cast<double>(luminance) * static_cast<double>(luminance);
      ++included;
    }
    if (included == 0) {
      return sample;
    }

    const auto meanChannel = [included](std::uint64_t sum) {
      return static_cast<float>(sum) / static_cast<float>(included) / 255.0F;
    };
    sample.color = rgba(meanChannel(sumR), meanChannel(sumG), meanChannel(sumB));
    sample.luminance = relativeLuminance(sample.color);

    const double mean = sumLuminance / static_cast<double>(included);
    const double variance = std::max(0.0, sumLuminanceSquared / static_cast<double>(included) - mean * mean);
    sample.spread = static_cast<float>(std::min(std::sqrt(variance), 1.0));

    float value = 0.0F;
    rgbToHsv(sample.color, sample.hueTurns, sample.saturation, value);
    sample.valid = true;
    return sample;
  }

  Style derive(const Sample& sample, const Color& onSurface, bool blurred) {
    Style style;
    if (!sample.valid) {
      return style;
    }

    const bool contentIsDark = sample.luminance < 0.5F;
    const float lightness = surfaceLightness(sample.luminance, contentIsDark);
    const float saturation =
        std::min(sample.saturation, contentIsDark ? kDarkBandMaxSaturation : kLightBandMaxSaturation);
    style.surface = hsl(sample.hueTurns * 360.0F, saturation, lightness);

    // Busy content needs a more opaque bar to stay readable; so does a surface
    // whose polarity runs against the surrounding desktop, where a see-through
    // bar would read as a mistake rather than as a deliberate tint. Blur buys
    // some of that back because the compositor is already smoothing the detail.
    const float polarityMismatch = contentIsDark == isLightPalette() ? 1.0F : 0.0F;
    float opacity = 0.55F + sample.spread * 0.35F + polarityMismatch * 0.25F;
    if (blurred) {
      opacity -= 0.05F;
    }
    style.opacity = std::clamp(opacity, 0.55F, 1.0F);

    // What the bar's ink actually lands on once its own alpha is applied. The
    // sampled strip is the closest available stand-in for the pixels behind the
    // bar, since it is the content the bar is anchored against.
    style.backdrop = lerpColor(sample.color, style.surface, style.opacity);

    // Keeping the palette's hue and chroma and moving only in lightness is what
    // separates this from a true inversion: a saturated blue backdrop stays blue
    // and simply takes dark ink, where 255 - channel would hand back orange.
    style.foreground = fromThemeColor(
        noctalia::theme::ensureContrast(toThemeColor(onSurface), toThemeColor(style.backdrop), kTextContrast)
    );
    style.valid = true;
    return style;
  }

  Color adjustAccent(const Color& accent, const Style& style) {
    if (!style.valid) {
      return accent;
    }
    return fromThemeColor(
        noctalia::theme::ensureContrast(toThemeColor(accent), toThemeColor(style.backdrop), kAccentContrast)
    );
  }

} // namespace noctalia::bar::tint
