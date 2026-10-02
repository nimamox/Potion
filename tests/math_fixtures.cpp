#include "potion/json.hpp"
#include "potion/markdown.hpp"
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

// Test-only bridge: exercise the real Markdown -> C++ -> Rust KaTeX path.
// No browser-side TeX parser or checked-in generated HTML is required.
int main() {
  try {
    const std::string input(std::istreambuf_iterator<char>(std::cin), {});
    const auto cases = potion::Json::parse(input);
    potion::ImageRegistry images;
    potion::MarkdownRenderer renderer(images);
    auto result = potion::Json::array();
    for (const auto &test : cases.items()) {
      const auto &expression = test.get("expression").string();
      const bool display = test.get("display").boolean();
      const auto markdown = display ? "$$\n" + expression + "\n$$\n"
                                    : "$" + expression + "$\n";
      auto item = test;
      const auto html = renderer.render(markdown);
      if (html.find("math-error") != std::string::npos ||
          html.find("class=\"katex") == std::string::npos)
        throw std::runtime_error("Native math failed: " + expression);
      item["html"] = potion::Json(html);
      result.items().push_back(std::move(item));
    }
    std::cout << result.dump() << '\n';
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
