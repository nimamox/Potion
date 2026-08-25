#pragma once
#include <string>

namespace potion {

class MathRenderer {
public:
  [[nodiscard]] std::string render(const std::string &latex,
                                   bool display_mode) const;
};

} // namespace potion
