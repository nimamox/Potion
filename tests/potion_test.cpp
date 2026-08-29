#include "potion/app_state.hpp"
#include "potion/http_server.hpp"
#include "potion/json.hpp"
#include "potion/markdown.hpp"
#include "potion/notion_client.hpp"
#include "potion/reading_positions.hpp"
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
  explicit RunningServer(const std::string &state, const std::string &start_page = {})
      : server([&] { potion::ServerOptions options; options.data_dir = state;
          options.port = 0; options.simulator = true; options.worker_count = 4;
          options.start_page_id = start_page;
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

    const auto repeated_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":"very important / very important","link":null},
       "plain_text":"very important / very important",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    potion::Json formatted;
    std::string format_error;
    require(potion::NotionClient::build_formatted_rich_text(
                repeated_rich_text, "very important / very important",
                17, 31, "very important", "bold", formatted, format_error),
            "format one repeated occurrence");
    require(formatted.items().size() == 2 &&
            !formatted.items()[0].get("annotations").get("bold").boolean() &&
            formatted.items()[1].get("annotations").get("bold").boolean(),
            "only selected repeated occurrence is bold");

    const auto emoji_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":"A😀B","link":null},
       "plain_text":"A😀B",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                emoji_rich_text, "A😀B", 1, 3, "😀", "underline",
                formatted, format_error) &&
            formatted.items().size() == 3 &&
            formatted.items()[1].get("annotations").get("underline").boolean(),
            "UTF-16 selection offsets preserve supplementary characters");
    format_error.clear();
    require(!potion::NotionClient::build_formatted_rich_text(
                emoji_rich_text, "A😀B", 2, 3, "😀", "highlight",
                formatted, format_error),
            "selection cannot split a UTF-16 surrogate pair");

    const auto mention_rich_text = potion::Json::parse(R"([
      {"type":"mention","mention":{"type":"page","page":{"id":"5908cc54-8ef3-42b6-b84e-254fa1785a21"}},
       "plain_text":"Mention","annotations":{"bold":false}}
    ])");
    format_error.clear();
    require(!potion::NotionClient::build_formatted_rich_text(
                mention_rich_text, "Mention", 0, 7, "Mention", "bold",
                formatted, format_error),
            "non-text rich text is rejected safely");

    potion::ImageRegistry images;
    potion::MarkdownRenderer renderer(images);
    const std::string html = renderer.render(
      "# Heading\n- parent\n\t- child\n- sibling\n"
      "[safe](https://example.com) [bad](javascript:alert(1))\n"
      "<script>alert(1)</script>\n$$\nx^2\n$$\n"
      "| Name | Value |\n|---|---|\n| Safe | **bold** |\n");
    require(html.find("<h1 class=\"potion-block\">Heading</h1>") != std::string::npos, "heading");
    require(html.find("<ul><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">parent</span><ul><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">child</span></li></ul></li><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">sibling</span></li></ul>") != std::string::npos, "nested list");
    require(html.find("javascript:") == std::string::npos, "unsafe URL leaked");
    require(html.find("<script>") == std::string::npos, "unsafe HTML leaked");
    require(html.find("display-math") != std::string::npos, "equation");
    require(html.find("class=\"katex-display\"") != std::string::npos,
            "display equation is rendered natively");
    require(html.find("katex-mathml") == std::string::npos &&
            html.find("<math") == std::string::npos,
            "native equation contains HTML only");
    require(html.find("<table class=\"potion-block\"><thead><tr><th>Name</th><th>Value</th></tr></thead><tbody><tr><td>Safe</td><td><strong>bold</strong></td></tr></tbody></table>") != std::string::npos, "pipe table");

    const std::string logical_blocks = renderer.render(
      "# Heading\nParagraph with **inline** text.\n- list item\n> quote\n"
      "```\ncode\n```\n$$\nx^2\n$$\n"
      "![image](https://example.com/logical.png)\n");
    require(logical_blocks.find("<h1 class=\"potion-block\">") != std::string::npos &&
            logical_blocks.find("<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">") != std::string::npos &&
            logical_blocks.find("<li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">") != std::string::npos &&
            logical_blocks.find("<blockquote class=\"potion-block\">") != std::string::npos &&
            logical_blocks.find("<pre class=\"potion-block\">") != std::string::npos &&
            logical_blocks.find("class=\"potion-block math display-math\"") != std::string::npos &&
            logical_blocks.find("<figure class=\"potion-block\">") != std::string::npos,
            "logical block elements are marked");
    require(logical_blocks.find("<strong class=\"potion-block") == std::string::npos &&
            logical_blocks.find("<span class=\"potion-block") == std::string::npos,
            "inline formatting is not a logical block");
    const std::string editable_scope = renderer.render(
      "Paragraph\n- list item\n- [ ] to do\n# Heading\n> Quote\n"
      "```\ncode\n```\n$$\nx\n$$\n![caption](https://example.com/image.png)\n");
    require(editable_scope.find("<p class=\"potion-block potion-editable\">") != std::string::npos &&
            editable_scope.find("<li class=\"potion-block potion-editable\">") != std::string::npos,
            "paragraphs and ordinary list items are editable");
    require(editable_scope.find("<li class=\"potion-block\"><span class=\"todo-box\">") != std::string::npos &&
            editable_scope.find("<h1 class=\"potion-block potion-editable\">") == std::string::npos &&
            editable_scope.find("<blockquote class=\"potion-block potion-editable\">") == std::string::npos &&
            editable_scope.find("<pre class=\"potion-block potion-editable\">") == std::string::npos &&
            editable_scope.find("<figure class=\"potion-block potion-editable\">") == std::string::npos,
            "unsupported block types are not editable");

    const std::string figure = renderer.render(
      "Impulse response $`h[n]`$\n"
      "![Impulse response $`h[n]`$](https://example.com/impulse.png)\n");
    require(figure.find("<img data-src=\"http://127.0.0.1:8766/api/images/") != std::string::npos, "lazy image source");
    require(figure.find("<figcaption>Impulse response <span class=\"math\"><span class=\"katex\">") != std::string::npos,
            "caption math is rendered natively");
    require(figure.find("potion-editable-content\">Impulse response") == std::string::npos && figure.find("<p><figure>") == std::string::npos, "deduplicated standalone figure");
    const std::string figure_first = renderer.render(
      "![Impulse response $`h[n]`$](https://example.com/impulse.png)\n"
      "Impulse response $`h[n]`$\n");
    require(figure_first.find("potion-editable-content\">Impulse response") == std::string::npos, "deduplicated caption after figure");

    const std::string native_math = renderer.render(
      "Inline $x_i^2$ and $\\frac{1}{2}$.\n"
      "$$\n\\sum_{k=1}^{N}x_k\n$$\n"
      "Malformed $\\notacommand$.\n");
    require(native_math.find("msupsub") != std::string::npos &&
            native_math.find("mfrac") != std::string::npos &&
            native_math.find("op-limits") != std::string::npos,
            "native scripts, fraction, and limits markup");
    require(native_math.find("<span class=\"math-error\">\\notacommand</span>") != std::string::npos,
            "malformed math uses an escaped fallback");

    const std::string caption_boundary = renderer.render(
      "# Toggle {toggle=\"true\"}\n"
      "\tCaption\n"
      "![Caption](https://example.com/image.png)\n");
    require(caption_boundary.find("<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Caption</span></p></div></div><figure class=\"potion-block\">") != std::string::npos,
            "caption deduplication preserves toggle closure");

    const std::string notion_blocks = renderer.render(
      "<page url=\"https://www.notion.so/Books-5908cc548ef342b6b84e254fa1785a21\">A child page</page>\n"
      "# Toggle heading {toggle=\"true\"}\n\tHidden under heading\n"
      "<details>\n<summary>Regular toggle</summary>\n\tHidden detail\n</details>\n");
    require(notion_blocks.find("data-page-id=\"5908cc548ef342b6b84e254fa1785a21\"") != std::string::npos, "child page link");
    require(notion_blocks.find("Unsupported Notion content") == std::string::npos, "supported Notion blocks");
    require(notion_blocks.find("heading-toggle") != std::string::npos, "toggle heading");
    require(notion_blocks.find("heading-toggle\"><button type=\"button\" class=\"potion-block toggle-summary\" aria-expanded=\"true\"") != std::string::npos, "toggle heading expanded");
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
    require(nested_blocks.find("<ul><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Ping</span></li><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Predict</span></li><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Profile</span></li></ul>") != std::string::npos, "toggle-relative bullet list");
    require(nested_blocks.find("<ol><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Delay the input</span></li><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Delay the output</span></li><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Compare</span></li></ol>") != std::string::npos, "toggle-relative numbered list");
    require(nested_blocks.find("<table class=\"potion-block\"><tr><td>Symbol</td><td>Meaning</td></tr></table></div></div></div></div><div class=\"toggle heading-toggle\"") != std::string::npos, "table does not break sibling toggles");

    const std::string sibling_table = renderer.render(
      "# Toggle {toggle=\"true\"}\n"
      "\tInside toggle\n"
      "<table>\n<tr>\n<td>Outside</td>\n</tr>\n</table>\n");
    require(sibling_table.find("<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Inside toggle</span></p></div></div><table class=\"potion-block\">") != std::string::npos,
            "sibling table remains outside toggle");

    potion::ImageRegistry edge_images;
    potion::MarkdownRenderer edge_renderer(edge_images);
    const std::string parenthesized = edge_renderer.render(
      "[link](https://example.com/path_(v1)/item?q=(x))\n"
      "![plot](https://example.com/plot(a).png)\n");
    require(parenthesized.find("href=\"https://example.com/path_(v1)/item?q=(x)\"") != std::string::npos,
            "balanced parentheses in link URL");
    std::string image_url;
    require(edge_images.resolve("1", image_url) && image_url == "https://example.com/plot(a).png",
            "balanced parentheses in image URL");

    potion::ImageRegistry http_images;
    potion::MarkdownRenderer http_renderer(http_images);
    const std::string http_image = http_renderer.render(
      "![HTTP-only image](http://example.com/image.png)\n");
    require(http_image.find("data-src=") == std::string::npos &&
            http_image.find("<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">HTTP-only image</span></p>") != std::string::npos,
            "HTTP image does not register a guaranteed-broken image");
    require(!http_images.resolve("1", image_url), "HTTP image registry remains empty");

    const std::string query_page = renderer.render(
      "<page url=\"https://www.notion.so/Books-5908cc548ef342b6b84e254fa1785a21?pvs=4\">Query page</page>\n"
      "<page url=\"https://www.notion.so/Books-5908cc548ef342b6b84e254fa1785a21#fragment\">Fragment page</page>\n");
    require(query_page.find("data-page-id=\"5908cc548ef342b6b84e254fa1785a21\"") != std::string::npos,
            "page ID survives query or fragment");
    require(query_page.find("Fragment page") != std::string::npos, "fragment page rendered");
    const std::string self_closing_page = renderer.render(
      "<mention-page url=\"https://www.notion.so/5908cc548ef342b6b84e254fa1785a21?pvs=4\"/>\n");
    require(self_closing_page.find("data-page-id=\"5908cc548ef342b6b84e254fa1785a21\"") != std::string::npos &&
            self_closing_page.find("&lt;mention-page") == std::string::npos,
            "self-closing page mention is clickable");

    const std::string backslashes = renderer.render(
      "Windows path: C:\\Users\\Nima\\Potion\n"
      "Escaped punctuation: \\*literal asterisks\\* and \\[literal brackets\\].\n"
      "| Path | Value |\n|---|---|\n| Windows | C:\\Users\\Nima |\n");
    require(backslashes.find("C:\\Users\\Nima\\Potion") != std::string::npos,
            "ordinary backslashes preserved");
    require(backslashes.find("literal asterisks") != std::string::npos &&
            backslashes.find("<em>literal asterisks</em>") == std::string::npos,
            "Markdown punctuation remains escapable");
    require(backslashes.find("C:\\Users\\Nima</td>") != std::string::npos,
            "ordinary backslashes preserved in table");

    const std::string attributes = renderer.render(
      "Set S = {1, 2, 3}\n"
      "The possible states are {idle, running, stopped}\n"
      "# Why toggle=\"true\" matters\n"
      "# Actual toggle {toggle=\"true\"}\n"
      "Visible text {color=\"orange\"}\n");
    require(attributes.find("Set S = {1, 2, 3}") != std::string::npos &&
            attributes.find("{idle, running, stopped}") != std::string::npos,
            "ordinary brace text is preserved");
    require(attributes.find("<h1 class=\"potion-block\">Why toggle=&quot;true&quot; matters</h1>") != std::string::npos &&
            attributes.find("toggle-heading-1\">Actual toggle") != std::string::npos,
            "only trailing Notion attributes create toggles");
    require(attributes.find("<p class=\"potion-block potion-editable notion-color notion-color-orange\"><span class=\"potion-editable-content\">Visible text</span></p>") != std::string::npos,
            "recognized trailing attributes are removed");

    const std::string escaped_markdown = renderer.render(
      "[link](https://example.com/a\\)b)\n"
      "*a \\* b*\n");
    require(escaped_markdown.find("href=\"https://example.com/a\\)b\"") != std::string::npos,
            "escaped closing parenthesis remains in URL");
    require(escaped_markdown.find("<em>a * b</em>") != std::string::npos,
            "escaped emphasis delimiter does not close emphasis");

    const std::string numbered_list = renderer.render("5. Fifth item\n6. Sixth item\n\n10000. Large item\n");
    require(numbered_list.find("<ol start=\"5\"><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Fifth item</span></li><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Sixth item</span></li>") != std::string::npos &&
            numbered_list.find("<ol start=\"10000\"><li class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Large item</span></li>") != std::string::npos,
            "ordered list starts and long indices are preserved");

    const std::string reverse_caption_depth = renderer.render(
      "# Toggle {toggle=\"true\"}\n"
      "\t![Caption](https://example.com/a.png)\n"
      "Caption\n");
    require(reverse_caption_depth.find("</div></div><p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Caption</span></p>") != std::string::npos,
            "caption outside image depth is retained");

    potion::ImageRegistry registry_lifetime;
    const std::string first_image = registry_lifetime.register_url("https://example.com/first.png");
    registry_lifetime.clear();
    const std::string second_image = registry_lifetime.register_url("https://example.com/second.png");
    require(first_image != second_image && registry_lifetime.resolve(second_image, image_url) &&
            image_url == "https://example.com/second.png", "image keys are never reused");

    const std::string rtl = renderer.render(
      "سلام عرض میکنم خدمت شما!\n"
      "Left-to-right paragraph.\n");
    require(rtl.find("<p class=\"potion-block potion-editable\" dir=\"rtl\"><span class=\"potion-editable-content\">سلام عرض میکنم خدمت شما!</span></p>") != std::string::npos,
            "right-to-left paragraph direction");
    require(rtl.find("dir=\"rtl\">Left-to-right") == std::string::npos,
            "left-to-right paragraph direction unchanged");

    const std::string highlighted = renderer.render(
      "<span color=\"yellow_bg\">Highlighted text</span>\n"
      "A highlighted paragraph {color=\"blue_background\"}\n");
    require(highlighted.find("notion-color-yellow-bg") != std::string::npos, "inline highlight");
    require(highlighted.find("notion-color-blue-bg") != std::string::npos, "block highlight");
    require(highlighted.find("Unsupported Notion content") == std::string::npos, "highlight supported");
    const std::string inline_only = renderer.render(
      "Plain <span color=\"orange\">orange words</span> remain plain\n");
    require(inline_only.find("<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Plain <span class=\"notion-color notion-color-orange\">") != std::string::npos, "inline color scope");
    require(inline_only.find("<p class=\"potion-block potion-editable notion-color") == std::string::npos, "inline color did not leak to block");

    char directory[] = "/tmp/potion-test.XXXXXX";
    require(::mkdtemp(directory) != nullptr, "mkdtemp");

    potion::PageUuid uuid;
    require(potion::parse_page_uuid("5908cc548ef342b6b84e254fa1785a21", uuid),
            "compact UUID decode");
    require(potion::format_page_uuid(uuid) == "5908cc54-8ef3-42b6-b84e-254fa1785a21",
            "UUID encode");
    potion::PageUuid hyphenated_uuid;
    require(potion::parse_page_uuid("5908cc54-8ef3-42b6-b84e-254fa1785a21", hyphenated_uuid) &&
            hyphenated_uuid == uuid, "hyphenated UUID decode");
    require(!potion::parse_page_uuid("5908cc54-8ef3-42b6-b84e254f-a1785a21", uuid),
            "malformed UUID rejected");

    const std::uint32_t position_now = 2000000000u;
    const std::uint16_t quarter = static_cast<std::uint16_t>(.25 * 65535.0 + .5);
    require(quarter == 16384, "block fraction fixed-point encoding");
    const std::string positions_dir = std::string(directory) + "/positions-roundtrip";
    require(::mkdir(positions_dir.c_str(), 0700) == 0, "positions directory");
    {
      potion::ReadingPositionStore positions(positions_dir, position_now);
      std::string error;
      require(positions.update("5908cc548ef342b6b84e254fa1785a21", 173, quarter,
                               position_now, error), "save reading position");
      require(positions.update("5908cc54-8ef3-42b6-b84e-254fa1785a21", 174, quarter,
                               position_now + 1, error), "update reading position");
      require(positions.size() == 1, "reading position update is unique");
    }
    {
      potion::ReadingPositionStore positions(positions_dir, position_now + 1);
      const auto saved = positions.get("5908cc548ef342b6b84e254fa1785a21");
      require(saved && saved->block_index == 174 && saved->block_fraction == quarter &&
              saved->last_seen == position_now + 1, "binary position round trip");
    }
    {
      std::ifstream input(positions_dir + "/positions.dat", std::ios::binary);
      const std::string bytes((std::istreambuf_iterator<char>(input)),
                              std::istreambuf_iterator<char>());
      require(bytes.size() == 42 && bytes.compare(0, 8, std::string("POTNPOS\0", 8)) == 0,
              "versioned position binary format");
    }

    const std::string malformed_dir = std::string(directory) + "/positions-malformed";
    require(::mkdir(malformed_dir.c_str(), 0700) == 0, "malformed directory");
    { std::ofstream output(malformed_dir + "/positions.dat", std::ios::binary); output << "not a position database"; }
    require(potion::ReadingPositionStore(malformed_dir, position_now).size() == 0,
            "malformed positions ignored");

    const std::string expiry_dir = std::string(directory) + "/positions-expiry";
    require(::mkdir(expiry_dir.c_str(), 0700) == 0, "expiry directory");
    {
      potion::ReadingPositionStore positions(expiry_dir, position_now);
      std::string error;
      require(positions.update("5908cc548ef342b6b84e254fa1785a21", 1, 2,
                               position_now, error), "expiry seed");
    }
    require(potion::ReadingPositionStore(expiry_dir,
            position_now + 30u * 24u * 60u * 60u + 1u).size() == 0,
            "positions expire after 30 days");

    const std::string cap_dir = std::string(directory) + "/positions-cap";
    require(::mkdir(cap_dir.c_str(), 0700) == 0, "cap directory");
    {
      potion::ReadingPositionStore positions(cap_dir, position_now);
      std::string error;
      for (std::uint32_t i = 0; i < 513; ++i) {
        potion::PageUuid generated;
        generated.bytes[12] = static_cast<std::uint8_t>(i >> 24);
        generated.bytes[13] = static_cast<std::uint8_t>(i >> 16);
        generated.bytes[14] = static_cast<std::uint8_t>(i >> 8);
        generated.bytes[15] = static_cast<std::uint8_t>(i);
        require(positions.update(potion::format_page_uuid(generated), i, 0,
                                 position_now + i, error), "cap seed");
      }
      potion::PageUuid newest;
      newest.bytes[14] = 2;
      require(positions.size() <= 512 &&
              positions.get(potion::format_page_uuid(newest)).has_value(),
              "512-entry LRU cap retains recent pages");
      potion::PageUuid oldest;
      require(!positions.get(potion::format_page_uuid(oldest)).has_value(),
              "512-entry LRU cap evicts oldest pages");
    }
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
      RunningServer running(directory, "3c5d2870a15280b48d7fe83c9f24b96e");
      const unsigned port = running.server.bound_port();
      require(potion::Json::parse(request(port, "/api/status")).get("startPageId").string() ==
                  "3c5d2870a15280b48d7fe83c9f24b96e",
              "startup page status");
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

      const std::string page_id = "5908cc548ef342b6b84e254fa1785a21";
      const std::string saved_position = "blockIndex=173&blockFraction=16384";
      request(port, "/api/pages/" + page_id + "/position", &saved_position);
      auto position = potion::Json::parse(request(port, "/api/pages/" + page_id + "/position")).get("position");
      require(position.get("blockIndex").number() == 173 &&
              position.get("blockFraction").number() == 16384,
              "reading position HTTP round trip");
      const std::string out_of_range = "blockIndex=999999999999&blockFraction=70000";
      request(port, "/api/pages/" + page_id + "/position", &out_of_range);
      position = potion::Json::parse(request(port, "/api/pages/" + page_id + "/position")).get("position");
      require(position.get("blockIndex").number() == 4294967295.0 &&
              position.get("blockFraction").number() == 65535,
              "reading position API clamps numeric range");
      const std::string empty_body;
      request(port, "/api/auth/logout", &empty_body);
      require(potion::Json::parse(request(port, "/api/pages/" + page_id + "/position"))
                  .get("position").is_null(), "logout clears reading positions");
      require(::access((std::string(directory) + "/positions.dat").c_str(), F_OK) != 0,
              "logout removes persisted positions");

      auto shutdown_wait = std::async(std::launch::async, [port] {
        try { request(port, "/api/input"); } catch (...) {}
      });
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      running.server.stop();
      require(shutdown_wait.wait_for(std::chrono::seconds(1)) == std::future_status::ready, "shutdown did not wake long-poll");
    }
    ::unlink((std::string(directory) + "/token").c_str());
    ::unlink((std::string(directory) + "/state.conf").c_str());
    const std::string position_test_dirs[] = {
      positions_dir, malformed_dir, expiry_dir, cap_dir
    };
    for (const auto &path : position_test_dirs) {
      ::unlink((path + "/positions.dat").c_str());
      ::unlink((path + "/positions.dat.tmp").c_str());
      ::rmdir(path.c_str());
    }
    ::rmdir(directory);
    std::cout << "Potion tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Potion test failure: " << error.what() << '\n';
    return 1;
  }
}
