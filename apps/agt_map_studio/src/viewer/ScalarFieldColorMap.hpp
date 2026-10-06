#pragma once

#include <optional>
#include <string>
#include <vector>

namespace agt_map_studio {

struct ScalarFieldColorRange {
  float minimum = 0.0F;
  float maximum = 1.0F;
};

class ScalarFieldColorMap {
public:
  static bool resolve_range(const std::vector<float> &values,
                            std::optional<float> minimum,
                            std::optional<float> maximum,
                            ScalarFieldColorRange *range, std::string *error);
  static bool map_to_rgb(const std::vector<float> &values,
                         const ScalarFieldColorRange &range,
                         std::vector<float> *rgb, std::string *error);
};

}  // namespace agt_map_studio
