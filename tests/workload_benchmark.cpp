#include "workload_benchmark.hpp"
#include "potion/http_server.hpp"
#include "potion/image_cache.hpp"
#include "potion/json.hpp"
#include "potion/markdown.hpp"
#include "potion/page_cache.hpp"
#include <atomic>
#include <fstream>
#include <memory>
#include <thread>
#include <unistd.h>
int main(int argc, char **argv) {
  try {
    bench::require(argc == 2, "usage: workload_benchmark DISPOSABLE_DIRECTORY");
    const std::string directory = argv[1];
    potion::ImageRegistry images;
    potion::MarkdownRenderer renderer(images);
    potion::MathRenderer math;
    std::string markdown;
    for (int i = 0; i < 40; ++i)
      markdown +=
          "# Section " + std::to_string(i) +
          " {toggle=\"true\"}\n\tA paragraph with **bold**, *italic*, `code`, "
          "[a link](https://example.com).\n\t- First item\n\t- Second "
          "item\n\t> A quotation\n\t```cpp\nint example = 42;\n```\n";
    bench::measure("markdown_page", 21,
                   [&] { return renderer.render(markdown); });
    potion::Json response = potion::Json::object();
    response["markdown"] = potion::Json(markdown);
    const auto wire = response.dump();
    bench::measure("page_json_parse_render", 21, [&] {
      auto page = potion::Json::parse(wire);
      auto html = renderer.render(page.get("markdown").string());
      potion::Json result = potion::Json::object();
      result["html"] = potion::Json(html);
      return result.dump();
    });
    std::vector<double> cold;
    uint64_t math_hash = 0;
    std::ofstream math_examples(directory + "/math.html");
    for (int i = 0; i < 48; ++i) {
      const auto formula = "\\frac{x_{" + std::to_string(i) +
                           "}^2+1}{\\sqrt{a^2+b^2}}+\\sum_{k=1}^{20}k+\\begin{"
                           "pmatrix}1&2\\\\3&4\\end{pmatrix}";
      auto start = bench::Clock::now();
      auto html = math.render(formula, true);
      cold.push_back(bench::ms(start));
      bench::require(html.find("katex") != std::string::npos, "math fallback");
      math_hash += bench::hash(html);
      math_examples << "<section data-example=\"" << i << "\">" << html
                    << "</section>\n";
    }
    bench::report("math_cold", cold, math_hash);
    bench::measure("math_cached", 101, [&] {
      return math.render("\\frac{x_{47}^2+1}{\\sqrt{a^2+b^2}}+\\sum_{k=1}^{20}"
                         "k+\\begin{pmatrix}1&2\\\\3&4\\end{pmatrix}",
                         true);
    });
    std::string with_images = markdown;
    for (int i = 0; i < 24; ++i)
      with_images += "![Image " + std::to_string(i) +
                     "](https://example.com/image-" + std::to_string(i) +
                     ".png)\n";
    with_images += "$$\\frac{1}{\\sqrt{1+x^2}}$$\n";
    bench::measure("markdown_math_images", 21, [&] {
      std::vector<std::string> keys;
      auto html = renderer.render(with_images, &keys);
      bench::require(keys.size() == 24, "image registry failed");
      return html;
    });
    potion::SessionImageCache cache(directory + "/images");
    std::string error;
    bench::require(cache.reset(error), "image cache reset failed");
    std::string bytes(256 * 1024, 'x');
    for (size_t i = 0; i < bytes.size(); ++i)
      bytes[i] = static_cast<char>(i * 71);
    const std::string key(64, 'a');
    bench::measure("image_cache_store_load_256k", 21, [&] {
      bench::require(cache.store(key, "image/png", bytes, cache.generation()),
                     "cache store failed");
      std::string type, body;
      bench::require(cache.load(key, type, body) && body == bytes &&
                         type == "image/png",
                     "cache read failed");
      return body;
    });
    potion::SessionPageCache pages(directory + "/pages");
    bench::require(pages.reset(error), "page cache reset failed");
    std::vector<potion::PageSummary> fixture;
    for (int i = 0; i < 500; ++i)
      fixture.push_back({std::to_string(i), "Example page " + std::to_string(i),
                         "", "2026-10-01"});
    bench::measure("page_snapshot_build_500", 9, [&] {
      std::vector<potion::PageSummary> result;
      bench::require(pages.get_or_build(
                         "fixture", true,
                         [&](std::string &) { return fixture; }, result,
                         error) &&
                         result.size() == 500,
                     "page snapshot failed");
      return std::to_string(result.size());
    });
    bench::measure("page_snapshot_cached_500", 21, [&] {
      std::vector<potion::PageSummary> result;
      bench::require(pages.get_or_build(
                         "fixture", false,
                         [&](std::string &) {
                           throw std::runtime_error("cache miss");
                           return fixture;
                         },
                         result, error) &&
                         result.size() == 500,
                     "page cache failed");
      return std::to_string(result.size());
    });
    bench::measure("server_construct", 7, [&] {
      potion::ServerOptions o;
      o.data_dir = directory + "/state";
      o.image_cache_dir = directory + "/http-images";
      o.page_cache_dir = directory + "/http-pages";
      o.simulator = true;
      o.port = 0;
      o.remote_setup_port = 0;
      o.update_checks_enabled = false;
      potion::HttpServer server(o);
      return std::string("ready");
    });
    cache.clear();
    pages.clear();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
