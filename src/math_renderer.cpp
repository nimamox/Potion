#include "potion/math_renderer.hpp"
#include <cstdint>
#include <iostream>

extern "C" {
char *potion_math_render(const char *latex, std::uint8_t display_mode);
void potion_math_free(char *html);
}

namespace potion {
namespace {
std::string html_escape(const std::string &text) {
  std::string output;
  output.reserve(text.size());
  for (const char character : text) {
    switch (character) {
    case '&': output += "&amp;"; break;
    case '<': output += "&lt;"; break;
    case '>': output += "&gt;"; break;
    case '"': output += "&quot;"; break;
    case '\'': output += "&#39;"; break;
    default: output += character; break;
    }
  }
  return output;
}

bool unbox(const std::string &latex, std::string &inner) {
  std::size_t first = latex.find_first_not_of(" \t\r\n");
  std::size_t last = latex.find_last_not_of(" \t\r\n");
  if (first == std::string::npos || latex.compare(first, 7, "\\boxed{") != 0 ||
      latex[last] != '}')
    return false;
  int depth = 0;
  for (std::size_t index = first + 6; index <= last; ++index) {
    if (latex[index] == '{') ++depth;
    else if (latex[index] == '}' && --depth == 0 && index != last) return false;
  }
  if (depth != 0) return false;
  inner = latex.substr(first + 7, last - first - 7);
  return true;
}
} // namespace

std::string MathRenderer::render(const std::string &latex,
                                 const bool display_mode) const {
  std::string expression;
  const bool boxed = unbox(latex, expression);
  if (!boxed) expression = latex;
  char *rendered = potion_math_render(expression.c_str(), display_mode ? 1 : 0);
  if (!rendered) {
    std::cerr << "Potion math fallback for unsupported expression\n";
    return "<span class=\"math-error\">" + html_escape(latex) + "</span>";
  }
  std::string html(rendered);
  potion_math_free(rendered);
  return boxed ? "<span class=\"potion-boxed-math potion-box-frame\">" +
                     html + "</span>" : html;
}

} // namespace potion
