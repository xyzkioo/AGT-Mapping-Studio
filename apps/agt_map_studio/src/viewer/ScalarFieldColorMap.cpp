#include "viewer/ScalarFieldColorMap.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace agt_map_studio {
namespace {

std::array<float, 3> mix(const std::array<float, 3> &a,
                         const std::array<float, 3> &b, float t) {
  return {a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
          a[2] + (b[2] - a[2]) * t};
}

std::array<float, 3> color_at(float t) {
  constexpr std::array<std::array<float, 3>, 5> colors{{
      {{0.18F, 0.24F, 0.86F}}, {{0.0F, 0.75F, 0.86F}},
      {{0.15F, 0.75F, 0.30F}}, {{0.95F, 0.80F, 0.12F}},
      {{0.86F, 0.10F, 0.10F}},
  }};
  const float position = std::clamp(t, 0.0F, 1.0F) * 4.0F;
  const std::size_t segment = std::min<std::size_t>(
      static_cast<std::size_t>(position), colors.size() - 2U);
  return mix(colors[segment], colors[segment + 1U],
             position - static_cast<float>(segment));
}

}  // namespace

bool ScalarFieldColorMap::resolve_range(
    const std::vector<float> &values, std::optional<float> minimum,
    std::optional<float> maximum, ScalarFieldColorRange *range,
    std::string *error) {
  if (!range) {
    if (error) *error = "color range result must not be null";
    return false;
  }
  float data_min = std::numeric_limits<float>::infinity();
  float data_max = -std::numeric_limits<float>::infinity();
  for (float value : values) {
    if (!std::isfinite(value)) continue;
    data_min = std::min(data_min, value);
    data_max = std::max(data_max, value);
  }
  if (!std::isfinite(data_min) || !std::isfinite(data_max)) {
    if (error) *error = "scalar field has no finite values";
    return false;
  }
  range->minimum = minimum.value_or(data_min);
  range->maximum = maximum.value_or(data_max);
  if (!std::isfinite(range->minimum) || !std::isfinite(range->maximum) ||
      range->minimum > range->maximum) {
    if (error) *error = "color minimum must be finite and no greater than maximum";
    return false;
  }
  return true;
}

bool ScalarFieldColorMap::map_to_rgb(
    const std::vector<float> &values, const ScalarFieldColorRange &range,
    std::vector<float> *rgb, std::string *error) {
  if (!rgb) {
    if (error) *error = "RGB result must not be null";
    return false;
  }
  if (!std::isfinite(range.minimum) || !std::isfinite(range.maximum) ||
      range.minimum > range.maximum) {
    if (error) *error = "invalid scalar color range";
    return false;
  }
  rgb->clear();
  rgb->reserve(values.size() * 3U);
  const float span = range.maximum - range.minimum;
  for (float value : values) {
    if (!std::isfinite(value)) {
      rgb->insert(rgb->end(), {0.50F, 0.50F, 0.50F});
      continue;
    }
    const float t = span > 0.0F ? (value - range.minimum) / span : 0.5F;
    const auto color = color_at(t);
    rgb->insert(rgb->end(), color.begin(), color.end());
  }
  return true;
}

}  // namespace agt_map_studio
