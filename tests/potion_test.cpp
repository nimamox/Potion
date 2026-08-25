#include "potion/app_state.hpp"
#include "potion/http_server.hpp"
#include "potion/json.hpp"
#include "potion/markdown.hpp"
#include <curl/curl.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}
size_t receive(char *data, size_t size, size_t count, void *opaque) {
  static_cast<std::string *>(opaque)->append(data, size * count);
  return size * count;
}
std::string request(unsigned port, const std::string &path,
                    const std::string *body = nullptr) {
  CURL *curl = curl_easy_init();
  if (!curl) throw std::runtime_error("curl init failed");
  std::string response;
  const std::string url = "http://127.0.0.1:" + std::to_string(port) + path;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  if (body) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
  }
  const CURLcode result = curl_easy_perform(curl);
  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(curl);
  if (result != CURLE_OK || status < 200 || status >= 300)
    throw std::runtime_error("HTTP request failed");
  return response;
}
struct RunningServer {
  potion::HttpServer server;
  std::thread thread;
  explicit RunningServer(const std::string &state)
      : server([&] { potion::ServerOptions options; options.data_dir = state;
          options.port = 0; options.simulator = true; options.worker_count = 4;
          options.input_timeout = std::chrono::milliseconds(120); return options; }()),
        thread([this] { server.run(); }) {
    for (int i = 0; i < 100 && server.bound_port() == 0; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    require(server.bound_port() != 0, "server did not bind");
  }
  ~RunningServer() { server.stop(); if (thread.joinable()) thread.join(); }
};
}

int main() {
  try {
    const auto parsed = potion::Json::parse(R"({"ok":true,"items":[1,"x"]})");
    require(parsed.get("ok").boolean(), "JSON bool");
    require(parsed.get("items").items().size() == 2, "JSON array");

    potion::ImageRegistry images;
    potion::MarkdownRenderer renderer(images);
    const std::string html = renderer.render(
      "# Heading\n- parent\n\t- child\n- sibling\n"
      "[safe](https://example.com) [bad](javascript:alert(1))\n"
      "<script>alert(1)</script>\n$$\nx^2\n$$\n"
      "| Name | Value |\n|---|---|\n| Safe | **bold** |\n");
    require(html.find("<h1>Heading</h1>") != std::string::npos, "heading");
    require(html.find("<ul><li>parent<ul><li>child</li></ul></li><li>sibling</li></ul>") != std::string::npos, "nested list");
    require(html.find("javascript:") == std::string::npos, "unsafe URL leaked");
    require(html.find("<script>") == std::string::npos, "unsafe HTML leaked");
    require(html.find("display-math") != std::string::npos, "equation");
    require(html.find("<table><thead><tr><th>Name</th><th>Value</th></tr></thead><tbody><tr><td>Safe</td><td><strong>bold</strong></td></tr></tbody></table>") != std::string::npos, "pipe table");

    const std::string figure = renderer.render(
      "Impulse response $`h[n]`$\n"
      "![Impulse response $`h[n]`$](https://example.com/impulse.png)\n");
    require(figure.find("<img data-src=\"http://127.0.0.1:8766/api/images/") != std::string::npos, "lazy image source");
    require(figure.find("<figcaption>Impulse response <span class=\"math\" data-expr=\"h[n]\">h[n]</span></figcaption>") != std::string::npos, "caption math");
    require(figure.find("<p>Impulse response") == std::string::npos && figure.find("<p><figure>") == std::string::npos, "deduplicated standalone figure");
    const std::string figure_first = renderer.render(
      "![Impulse response $`h[n]`$](https://example.com/impulse.png)\n"
      "Impulse response $`h[n]`$\n");
    require(figure_first.find("<p>Impulse response") == std::string::npos, "deduplicated caption after figure");

    const std::string notion_blocks = renderer.render(
      "<page url=\"https://www.notion.so/Books-5908cc548ef342b6b84e254fa1785a21\">A child page</page>\n"
      "# Toggle heading {toggle=\"true\"}\n\tHidden under heading\n"
      "<details>\n<summary>Regular toggle</summary>\n\tHidden detail\n</details>\n");
    require(notion_blocks.find("data-page-id=\"5908cc548ef342b6b84e254fa1785a21\"") != std::string::npos, "child page link");
    require(notion_blocks.find("Unsupported Notion content") == std::string::npos, "supported Notion blocks");
    require(notion_blocks.find("heading-toggle") != std::string::npos, "toggle heading");
    require(notion_blocks.find("heading-toggle\"><button type=\"button\" class=\"toggle-summary\" aria-expanded=\"true\"") != std::string::npos, "toggle heading expanded");
    require(notion_blocks.find("toggle-heading-1\">Toggle heading</span></button><div class=\"toggle-content\">") != std::string::npos, "toggle heading content visible");
    require(notion_blocks.find("Regular toggle") != std::string::npos && notion_blocks.find("toggle-content hidden") != std::string::npos, "regular toggle");

    const std::string nested_blocks = renderer.render(
      "# First section {toggle=\"true\"}\n"
      "\t## Review {toggle=\"true\"}\n"
      "\t\t- Ping\n\t\t- Predict\n\t\t- Profile\n"
      "\t\tThe standard test is:\n"
      "\t\t1. Delay the input\n\t\t2. Delay the output\n\t\t3. Compare\n"
      "\t\t<table>\n<tr>\n<td>Symbol</td>\n<td>Meaning</td>\n</tr>\n\t\t</table>\n"
      "# Second section {toggle=\"true\"}\n\tSecond content\n");
    require(nested_blocks.find("<ul><li>Ping</li><li>Predict</li><li>Profile</li></ul>") != std::string::npos, "toggle-relative bullet list");
    require(nested_blocks.find("<ol><li>Delay the input</li><li>Delay the output</li><li>Compare</li></ol>") != std::string::npos, "toggle-relative numbered list");
    require(nested_blocks.find("<table><tr><td>Symbol</td><td>Meaning</td></tr></table></div></div></div></div><div class=\"toggle heading-toggle\"") != std::string::npos, "table does not break sibling toggles");

    const std::string highlighted = renderer.render(
      "<span color=\"yellow_bg\">Highlighted text</span>\n"
      "A highlighted paragraph {color=\"blue_background\"}\n");
    require(highlighted.find("notion-color-yellow-bg") != std::string::npos, "inline highlight");
    require(highlighted.find("notion-color-blue-bg") != std::string::npos, "block highlight");
    require(highlighted.find("Unsupported Notion content") == std::string::npos, "highlight supported");
    const std::string inline_only = renderer.render(
      "Plain <span color=\"orange\">orange words</span> remain plain\n");
    require(inline_only.find("<p>Plain <span class=\"notion-color notion-color-orange\">") != std::string::npos, "inline color scope");
    require(inline_only.find("<p class=\"notion-color") == std::string::npos, "inline color did not leak to block");

    char directory[] = "/tmp/potion-test.XXXXXX";
    require(::mkdtemp(directory) != nullptr, "mkdtemp");
    {
      potion::AppState state(directory); std::string error;
      require(state.save_token("test-token", error), "save token");
      struct stat info{};
      require(::stat((std::string(directory) + "/token").c_str(), &info) == 0,
              "token file");
      require((info.st_mode & 0777) == 0600, "token permissions");
      require(state.set_setting("cardFont", "Palatino", error), "save setting");
      require(state.set_setting("nightPageMode", "palette-images", error), "save night page mode");
      require(state.settings_json().find("\"nightPageMode\":\"palette-images\"") != std::string::npos, "night page mode JSON");
      require(state.set_setting("pageButtonMode", "reversed", error), "save page button mode");
      require(state.settings_json().find("\"pageButtonMode\":\"reversed\"") != std::string::npos, "page button mode JSON");
    }
    require(potion::AppState(directory).token() == "test-token", "reload token");

    {
      potion::AppState state(directory);
      const std::string import_path = std::string(directory) + "/notion-token.txt";
      { std::ofstream out(import_path); out << "  ntn_imported-token\n"; }
      std::string message;
      const auto imported = potion::import_token_file(
          import_path, state,
          [](const std::string &token, std::string &) {
            return token == "ntn_imported-token";
          },
          message);
      require(imported == potion::TokenImportResult::imported, "import token");
      require(state.token() == "ntn_imported-token", "imported token stored");
      require(::access(import_path.c_str(), F_OK) != 0, "import file removed");

      { std::ofstream out(import_path); out << "invalid-token\n"; }
      const auto rejected = potion::import_token_file(
          import_path, state,
          [](const std::string &, std::string &error) {
            error = "Notion rejected the token"; return false;
          },
          message);
      require(rejected == potion::TokenImportResult::rejected, "reject token");
      require(state.token() == "ntn_imported-token", "rejected token not stored");
      require(::access(import_path.c_str(), F_OK) == 0, "rejected import retained");
      ::unlink(import_path.c_str());
    }

    {
      RunningServer running(directory);
      const unsigned port = running.server.bound_port();
      const auto start = std::chrono::steady_clock::now();
      const auto idle = potion::Json::parse(request(port, "/api/input"));
      require(idle.get("action").string().empty(), "idle long-poll action");
      require(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(80),
              "input endpoint busy-polled");

      auto waiting = std::async(std::launch::async, [port] {
        return potion::Json::parse(request(port, "/api/input")).get("action").string();
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
      const auto status_start = std::chrono::steady_clock::now();
      require(potion::Json::parse(request(port, "/api/status")).get("type").string() == "status", "concurrent HTTP request");
      require(std::chrono::steady_clock::now() - status_start < std::chrono::milliseconds(80), "long-poll blocked another request");
      const std::string forward = "action=forward";
      request(port, "/api/simulator/input", &forward);
      require(waiting.get() == "forward", "long-poll wake");

      const std::string backward = "action=backward";
      request(port, "/api/simulator/input", &forward);
      request(port, "/api/simulator/input", &backward);
      require(potion::Json::parse(request(port, "/api/input")).get("action").string() == "forward", "FIFO first");
      require(potion::Json::parse(request(port, "/api/input")).get("action").string() == "backward", "FIFO second");

      auto shutdown_wait = std::async(std::launch::async, [port] {
        try { request(port, "/api/input"); } catch (...) {}
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      running.server.stop();
      require(shutdown_wait.wait_for(std::chrono::seconds(1)) == std::future_status::ready, "shutdown did not wake long-poll");
    }
    ::unlink((std::string(directory) + "/token").c_str());
    ::unlink((std::string(directory) + "/state.conf").c_str());
    ::rmdir(directory);
    std::cout << "Potion tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Potion test failure: " << error.what() << '\n';
    return 1;
  }
}
