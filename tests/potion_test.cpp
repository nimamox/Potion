#include "potion/app_state.hpp"
#include "potion/http_server.hpp"
#include "potion/image_cache.hpp"
#include "potion/json.hpp"
#include "potion/markdown.hpp"
#include "potion/notion_client.hpp"
#include "potion/orientation.hpp"
#include "potion/page_cache.hpp"
#include "potion/reading_positions.hpp"
#include <curl/curl.h>
#include <atomic>
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
                    const std::string *body = nullptr);
struct HttpResponse {
  long status{};
  std::string headers;
  std::string body;
};
HttpResponse raw_request(unsigned port, const std::string &path,
                         const std::string *body = nullptr) {
  CURL *curl = curl_easy_init();
  if (!curl) throw std::runtime_error("curl init failed");
  HttpResponse response;
  const std::string url = "http://127.0.0.1:" + std::to_string(port) + path;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, receive);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  if (body) {
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body->c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body->size()));
  }
  const CURLcode result = curl_easy_perform(curl);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
  curl_easy_cleanup(curl);
  if (result != CURLE_OK)
    throw std::runtime_error("HTTP request failed");
  return response;
}
std::string request(unsigned port, const std::string &path,
                    const std::string *body) {
  const auto response = raw_request(port, path, body);
  if (response.status < 200 || response.status >= 300)
    throw std::runtime_error("HTTP request failed");
  return response.body;
}
struct RunningServer {
  potion::HttpServer server;
  std::thread thread;
  explicit RunningServer(
      const std::string &state, const std::string &start_page = {},
      std::function<bool(const std::string &, std::string &)> validator = {},
      std::function<void(potion::ServerOptions &)> configure = {})
      : server([&] { potion::ServerOptions options; options.data_dir = state;
          options.port = 0; options.simulator = true; options.worker_count = 4;
          options.remote_setup_port = 0;
          options.image_cache_dir = state + "/image-cache";
          options.page_cache_dir = state + "/page-cache";
          options.asset_dir = POTION_TEST_ASSET_DIR;
          options.start_page_id = start_page;
          options.token_validator = std::move(validator);
          if (configure) configure(options);
          return options; }()),
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
    {
      std::vector<std::vector<std::string>> commands;
      const potion::OrientationCommandRunner runner =
          [&commands](const std::vector<std::string> &arguments, std::string &) {
            commands.push_back(arguments);
            return true;
          };
      std::string error;
      require(potion::apply_kindle_rotation("auto", runner, error),
              "apply Kindle auto rotation");
      require(commands.size() == 1 && commands[0].size() == 4 &&
                  commands[0][2] == "orientationLock" &&
                  commands[0][3] == "off",
              "auto rotation clears the global orientation lock");
      commands.clear();
      require(potion::apply_kindle_rotation("locked", runner, error),
              "apply Kindle current-orientation lock");
      require(commands.size() == 1 && commands[0][3] == "current",
              "rotation lock captures the accelerometer's current direction");
      commands.clear();
      require(!potion::apply_kindle_rotation("sideways", runner, error) &&
                  commands.empty(),
              "invalid rotation mode does not invoke LIPC");
    }
    const auto parsed = potion::Json::parse(R"({"ok":true,"items":[1,"x"]})");
    require(parsed.get("ok").boolean(), "JSON bool");
    require(parsed.get("items").items().size() == 2, "JSON array");
    require(potion::Json::parse(R"("\uD83D\uDE00")").string() ==
                "\xf0\x9f\x98\x80",
            "JSON parser combines UTF-16 surrogate pairs");
    const auto rejects_json = [](const std::string &source) {
      try { potion::Json::parse(source); } catch (...) { return true; }
      return false;
    };
    require(rejects_json(R"("\uD83D")") &&
                rejects_json(R"("\uDE00")") &&
                rejects_json(R"("\uD83D\u0041")"),
            "JSON parser rejects unmatched UTF-16 surrogates");

    const auto empty_paragraph = potion::Json::parse(R"({
      "type":"paragraph","paragraph":{"rich_text":[]}})");
    const auto text_paragraph = potion::Json::parse(R"({
      "type":"paragraph","paragraph":{"rich_text":[
        {"type":"text","plain_text":"after empty","text":{"content":"after empty"}}
      ]}})");
    const auto text_callout = potion::Json::parse(R"({
      "type":"callout","callout":{"rich_text":[
        {"type":"text","plain_text":"Callout text","text":{"content":"Callout text"}}
      ]}})");
    const auto empty_callout = potion::Json::parse(R"({
      "type":"callout","callout":{"rich_text":[]}})");
    require(!potion::NotionClient::block_counts_as_editable(empty_paragraph) &&
            !potion::NotionClient::block_counts_as_editable(empty_callout) &&
            potion::NotionClient::block_counts_as_editable(text_paragraph) &&
            potion::NotionClient::block_counts_as_editable(text_callout),
            "backend editable indices include visible callout text but skip empty paragraphs");

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

    const auto trimmed_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":" This is the second paragraph. ","link":null},
       "plain_text":" This is the second paragraph. ",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                trimmed_rich_text, "This is the second paragraph.",
                12, 28, "second paragraph", "bold", formatted,
                format_error) &&
            formatted.items().size() == 3 &&
            formatted.items()[0].get("text").get("content").string() ==
                " This is the " &&
            formatted.items()[1].get("text").get("content").string() ==
                "second paragraph" &&
            formatted.items()[1].get("annotations").get("bold").boolean() &&
            formatted.items()[2].get("text").get("content").string() == ". ",
            "formatting maps trimmed Markdown offsets back to Notion whitespace");

    const auto mixed_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":"mixed ","link":null},"plain_text":"mixed ",
       "annotations":{"bold":true,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}},
      {"type":"text","text":{"content":"bold","link":null},"plain_text":"bold",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    bool format_enabled = false;
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                mixed_rich_text, "mixed bold", 0, 10, "mixed bold", "bold",
                formatted, format_error, &format_enabled) && format_enabled &&
            formatted.items()[0].get("annotations").get("bold").boolean() &&
            formatted.items()[1].get("annotations").get("bold").boolean(),
            "mixed bold selection becomes wholly bold");
    const auto all_bold = potion::Json::parse(R"([
      {"type":"text","text":{"content":"mixed ","link":null},"plain_text":"mixed ",
       "annotations":{"bold":true,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}},
      {"type":"text","text":{"content":"bold","link":null},"plain_text":"bold",
       "annotations":{"bold":true,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                all_bold, "mixed bold", 0, 10, "mixed bold", "bold",
                formatted, format_error, &format_enabled) && !format_enabled &&
            !formatted.items()[0].get("annotations").get("bold").boolean() &&
            !formatted.items()[1].get("annotations").get("bold").boolean(),
            "wholly bold selection toggles bold off");

    const auto decorated_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":"decorated","link":null},"plain_text":"decorated",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":true,"code":false,"color":"yellow_background"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                decorated_rich_text, "decorated", 0, 9, "decorated", "underline",
                formatted, format_error, &format_enabled) && !format_enabled &&
            !formatted.items()[0].get("annotations").get("underline").boolean(),
            "wholly underlined selection toggles underline off");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                decorated_rich_text, "decorated", 0, 9, "decorated", "highlight",
                formatted, format_error, &format_enabled) && !format_enabled &&
            formatted.items()[0].get("annotations").get("color").string() == "default",
            "wholly highlighted selection toggles highlight off");

    const auto linked_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":"linked text","link":{"url":"https://example.com"}},
       "plain_text":"linked text","href":"https://example.com",
       "annotations":{"bold":true,"italic":true,"strikethrough":true,"underline":true,"code":true,"color":"orange_background"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                linked_rich_text, "linked text", 0, 11, "linked text", "clear",
                formatted, format_error, &format_enabled) && !format_enabled &&
            !formatted.items()[0].get("annotations").get("bold").boolean() &&
            !formatted.items()[0].get("annotations").get("italic").boolean() &&
            !formatted.items()[0].get("annotations").get("strikethrough").boolean() &&
            !formatted.items()[0].get("annotations").get("underline").boolean() &&
            formatted.items()[0].get("annotations").get("code").boolean() &&
            formatted.items()[0].get("annotations").get("color").string() == "default" &&
            formatted.items()[0].get("text").get("link").get("url").string() == "https://example.com",
            "clear formatting preserves links and code while removing visual annotations");

    const auto relative_link_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":"Morbi","link":{"url":"/p/5908cc548ef342b6b84e254fa1785a21?pvs=25"}},
       "plain_text":"Morbi","href":"/p/5908cc548ef342b6b84e254fa1785a21?pvs=25",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}},
      {"type":"text","text":{"content":" aliquam","link":null},"plain_text":" aliquam",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                relative_link_rich_text, "Morbi aliquam", 6, 13, "aliquam",
                "bold", formatted, format_error) &&
            formatted.items()[0].get("text").get("link").get("url").string() ==
                "https://www.notion.so/p/5908cc548ef342b6b84e254fa1785a21?pvs=25" &&
            formatted.items().size() == 3 &&
            formatted.items()[2].get("annotations").get("bold").boolean(),
            "formatting adjacent text preserves and normalizes a relative Notion link");

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

    const auto multiline_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":"Line one\nLine two","link":null},
       "plain_text":"Line one\nLine two",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                multiline_rich_text, "Line one\nLine two", 9, 17, "Line two",
                "underline", formatted, format_error) &&
            formatted.items().size() == 2 &&
            formatted.items()[0].get("text").get("content").string() == "Line one\n" &&
            formatted.items()[1].get("annotations").get("underline").boolean(),
            "line-break-aware offsets format text after a Notion break");

    const auto mention_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":"Hello ","link":null},"plain_text":"Hello ",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}},
      {"type":"mention","mention":{"type":"user","user":{"id":"user-id","name":"Nima","avatar_url":null,"type":"person","person":{"email":"nima@example.com"}}},
       "plain_text":"Nima","annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}},
      {"type":"text","text":{"content":", important","link":null},"plain_text":", important",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    const std::string atom("\xef\xbf\xbc");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                mention_rich_text, "Hello " + atom + ", important", 9, 18, "important",
                "bold", formatted, format_error) &&
            formatted.items().size() == 4 &&
            formatted.items()[1].get("type").string() == "mention" &&
            formatted.items()[1].get("mention").get("user").get("id").string() == "user-id" &&
            formatted.items()[1].get("mention").get("user").get("name").is_null() &&
            formatted.items()[1].get("mention").get("user").get("person").is_null() &&
            formatted.items()[3].get("annotations").get("bold").boolean(),
            "a non-selected mention is preserved while adjacent text is formatted");
    format_error.clear();
    require(!potion::NotionClient::build_formatted_rich_text(
                mention_rich_text, "Hello " + atom + ", important", 6, 7, atom, "bold",
                formatted, format_error),
            "a selection overlapping a mention is rejected safely");
    format_error.clear();
    require(!potion::NotionClient::build_formatted_rich_text(
                mention_rich_text, "Hello " + atom + ", important", 6, 7, atom,
                "highlight", formatted, format_error),
            "highlighting a mention is still rejected safely");

    const auto equation_rich_text = potion::Json::parse(R"([
      {"type":"text","text":{"content":"Before ","link":null},"plain_text":"Before ",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}},
      {"type":"equation","equation":{"expression":"x^2"},"plain_text":"x^2",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}},
      {"type":"text","text":{"content":" after","link":null},"plain_text":" after",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                equation_rich_text, "Before " + atom + " after", 9, 14, "after", "underline",
                formatted, format_error) &&
            formatted.items()[1].get("type").string() == "equation" &&
            formatted.items()[1].get("equation").get("expression").string() == "x^2" &&
            formatted.items()[3].get("annotations").get("underline").boolean(),
            "a non-selected equation is preserved while adjacent text is formatted");

    const auto equation_highlight = potion::Json::parse(R"([
      {"type":"text","text":{"content":"Before ","link":null},"plain_text":"Before ",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}},
      {"type":"equation","equation":{"expression":"x^2"},"plain_text":"x^2",
       "annotations":{"bold":true,"italic":true,"strikethrough":false,"underline":true,"code":false,"color":"default"}},
      {"type":"text","text":{"content":" after","link":null},"plain_text":" after",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"default"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                equation_highlight, "Before " + atom + " after", 0, 14,
                "Before " + atom + " after", "highlight", formatted,
                format_error, &format_enabled) && format_enabled &&
            formatted.items().size() == 3 &&
            formatted.items()[0].get("text").get("content").string() == "Before " &&
            formatted.items()[0].get("annotations").get("color").string() ==
                "yellow_background" &&
            formatted.items()[1].get("type").string() == "equation" &&
            formatted.items()[1].get("equation").get("expression").string() ==
                "x^2" &&
            formatted.items()[1].get("annotations").get("color").string() ==
                "yellow_background" &&
            formatted.items()[1].get("annotations").get("bold").boolean() &&
            formatted.items()[1].get("annotations").get("italic").boolean() &&
            formatted.items()[1].get("annotations").get("underline").boolean() &&
            formatted.items()[2].get("text").get("content").string() == " after" &&
            formatted.items()[2].get("annotations").get("color").string() ==
                "yellow_background",
            "text and an atomic inline equation are highlighted together");

    const auto highlighted_equation = potion::Json::parse(R"([
      {"type":"text","text":{"content":"Before ","link":null},"plain_text":"Before ",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"yellow_background"}},
      {"type":"equation","equation":{"expression":"x^2"},"plain_text":"x^2",
       "annotations":{"bold":true,"italic":true,"strikethrough":false,"underline":true,"code":false,"color":"yellow_background"}},
      {"type":"text","text":{"content":" after","link":null},"plain_text":" after",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":false,"code":false,"color":"yellow_background"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                highlighted_equation, "Before " + atom + " after", 0, 14,
                "Before " + atom + " after", "highlight", formatted,
                format_error, &format_enabled) && !format_enabled &&
            formatted.items().size() == 3 &&
            formatted.items()[0].get("text").get("content").string() == "Before " &&
            formatted.items()[0].get("annotations").get("color").string() ==
                "default" &&
            formatted.items()[1].get("equation").get("expression").string() ==
                "x^2" &&
            formatted.items()[1].get("annotations").get("color").string() ==
                "default" &&
            formatted.items()[1].get("annotations").get("bold").boolean() &&
            formatted.items()[1].get("annotations").get("italic").boolean() &&
            formatted.items()[1].get("annotations").get("underline").boolean() &&
            formatted.items()[2].get("text").get("content").string() == " after" &&
            formatted.items()[2].get("annotations").get("color").string() ==
                "default",
            "highlight toggles off across text and an equation without changing other annotations");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                equation_rich_text, "Before " + atom + " after", 0, 14,
                "Before " + atom + " after", "underline", formatted,
                format_error, &format_enabled) && format_enabled &&
            formatted.items().size() == 3 &&
            formatted.items()[0].get("annotations").get("underline").boolean() &&
            formatted.items()[1].get("annotations").get("underline").boolean() &&
            formatted.items()[1].get("equation").get("expression").string() ==
                "x^2" &&
            formatted.items()[2].get("annotations").get("underline").boolean(),
            "text and an atomic inline equation are underlined together");
    const auto underlined_equation = potion::Json::parse(R"([
      {"type":"text","text":{"content":"Before ","link":null},"plain_text":"Before ",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":true,"code":false,"color":"default"}},
      {"type":"equation","equation":{"expression":"x^2"},"plain_text":"x^2",
       "annotations":{"bold":false,"italic":true,"strikethrough":false,"underline":true,"code":false,"color":"default"}},
      {"type":"text","text":{"content":" after","link":null},"plain_text":" after",
       "annotations":{"bold":false,"italic":false,"strikethrough":false,"underline":true,"code":false,"color":"default"}}
    ])");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                underlined_equation, "Before " + atom + " after", 0, 14,
                "Before " + atom + " after", "underline", formatted,
                format_error, &format_enabled) && !format_enabled &&
            !formatted.items()[0].get("annotations").get("underline").boolean() &&
            !formatted.items()[1].get("annotations").get("underline").boolean() &&
            formatted.items()[1].get("annotations").get("italic").boolean() &&
            !formatted.items()[2].get("annotations").get("underline").boolean(),
            "underline toggles off across text and an equation while preserving other annotations");
    format_error.clear();
    require(potion::NotionClient::build_formatted_rich_text(
                equation_highlight, "Before " + atom + " after", 0, 14,
                "Before " + atom + " after", "clear", formatted,
                format_error, &format_enabled) && !format_enabled &&
            formatted.items().size() == 3 &&
            formatted.items()[1].get("equation").get("expression").string() ==
                "x^2" &&
            !formatted.items()[1].get("annotations").get("bold").boolean() &&
            !formatted.items()[1].get("annotations").get("italic").boolean() &&
            !formatted.items()[1].get("annotations").get("strikethrough").boolean() &&
            !formatted.items()[1].get("annotations").get("underline").boolean() &&
            formatted.items()[1].get("annotations").get("color").string() ==
                "default",
            "clear removes visual formatting from an equation without changing it");
    format_error.clear();
    require(!potion::NotionClient::build_formatted_rich_text(
                equation_highlight, "Before " + atom + " after", 7, 8, atom,
                "bold", formatted, format_error),
            "unsupported bold formatting of an equation remains rejected");
    format_error.clear();
    require(!potion::NotionClient::build_formatted_rich_text(
                mention_rich_text, "Hello " + atom + ", important", 6, 7, atom,
                "clear", formatted, format_error),
            "clearing formatting from a mention remains rejected safely");

    const std::string colored_equation_markdown =
        "Because <span color=\"blue\">$`\\Sigma`$</span> and `$ignored$`, "
        "then <span underline=\"true\" color=\"yellow_background\">"
        "$`x^2`$</span>.\n$$\ndisplay = math\n$$\n";
    require(colored_equation_markdown.find(
                "<span color=\"blue\">$`\\Sigma`$</span>") !=
                std::string::npos &&
            colored_equation_markdown.find(
                "<span underline=\"true\" color=\"yellow_background\">$`x^2`$</span>") !=
                std::string::npos &&
            colored_equation_markdown.find("`$ignored$`") != std::string::npos &&
            colored_equation_markdown.find("$$\ndisplay = math\n$$") !=
                std::string::npos,
            "renderer accepts inline equation annotation markup safely");

    potion::ImageRegistry images;
    const std::string signed_image_url =
        "https://example.com/image.png?token=abc&x=1";
    const std::string signed_image_url_2 =
        "https://example.com/image.png?token=def&x=1";
    const std::string image_key = images.register_url(signed_image_url);
    require(image_key ==
                "9aa8938dd745f77c80202aa766c758616ce5d09c3b994adca04a4c98e02d4205",
            "image key uses stable SHA-256 of the complete URL");
    require(images.register_url(signed_image_url) == image_key,
            "identical image URLs produce identical keys");
    require(images.register_url(signed_image_url_2) != image_key,
            "different signed image URLs produce different keys");
    std::string viewbox_svg =
        "<?xml version=\"1.0\"?><svg xmlns=\"http://www.w3.org/2000/svg\" "
        "viewBox=\"0 0 1200 675\"><rect width=\"1200\" height=\"675\"/></svg>";
    potion::NotionClient::add_svg_intrinsic_dimensions(viewbox_svg);
    require(viewbox_svg.find(" width=\"1200\" height=\"675\">") !=
                std::string::npos,
            "viewBox-only SVG gains intrinsic dimensions for Mesquite");
    std::string sized_svg =
        "<svg width=\"640\" height=\"360\" viewBox=\"0 0 1200 675\"></svg>";
    potion::NotionClient::add_svg_intrinsic_dimensions(sized_svg);
    require(sized_svg.find("width=\"1200\"") == std::string::npos,
            "explicit SVG dimensions remain unchanged");
    std::string malformed_svg = "<svg viewBox=\"0 0 nope 675\"></svg>";
    potion::NotionClient::add_svg_intrinsic_dimensions(malformed_svg);
    require(malformed_svg.find(" width=") == std::string::npos,
            "malformed SVG viewBox is not rewritten");
    potion::ImageRegistry independent_images;
    require(independent_images.register_url(signed_image_url) == image_key,
            "image keys are deterministic across registry instances");
    std::string resolved_image_url;
    require(images.resolve(image_key, resolved_image_url) &&
                resolved_image_url == signed_image_url,
            "deterministic image key resolves to the complete original URL");

    potion::ImageRegistry bounded_images;
    const std::string oldest_key = bounded_images.register_url(
        "https://example.com/image-0.png");
    const std::string second_oldest_key = bounded_images.register_url(
        "https://example.com/image-1.png");
    for (int i = 2; i < 1024; ++i)
      bounded_images.register_url(
          "https://example.com/image-" + std::to_string(i) + ".png");
    require(bounded_images.resolve(oldest_key, resolved_image_url),
            "resolving an image refreshes its LRU position");
    const std::string newest_key = bounded_images.register_url(
        "https://example.com/image-1024.png");
    require(bounded_images.resolve(oldest_key, resolved_image_url) &&
                !bounded_images.resolve(second_oldest_key, resolved_image_url) &&
                bounded_images.resolve(newest_key, resolved_image_url),
            "image registry evicts the least recently used entry at its bound");

    const std::string notion_block_id =
        "1234567890abcdef1234567890abcdef";
    const std::string notion_image_url =
        "https://prod-files-secure.s3.us-west-2.amazonaws.com/"
        "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/" + notion_block_id +
        "/diagram.png?X-Amz-Signature=old";
    const std::string notion_image_key = images.register_url(notion_image_url);
    potion::ImageSource notion_source;
    require(images.resolve_source(notion_image_key, notion_source) &&
                notion_source.notion_hosted() &&
                notion_source.notion_block_id == notion_block_id,
            "Notion-hosted image retains block provenance");

    potion::ImageRegistry expired_registry;
    const std::string expired_key = expired_registry.register_notion_url(
        notion_image_url, notion_block_id, 900);
    int expired_downloads = 0, expired_refreshes = 0;
    potion::BinaryResponse refreshed_image;
    std::string image_error;
    require(potion::retrieve_registered_image(
                expired_registry, expired_key, 1000,
                [&](const std::string &url, potion::BinaryResponse &result,
                    std::string &, long *status) {
                  ++expired_downloads;
                  if (status) *status = 200;
                  if (url.find("Signature=fresh") == std::string::npos)
                    return false;
                  result = {"image/png", "fresh image"};
                  return true;
                },
                [&](const std::string &block_id, std::string &url,
                    std::int64_t &expires_at, std::string &) {
                  ++expired_refreshes;
                  require(block_id == notion_block_id,
                          "refresh uses the image block id");
                  url = notion_image_url.substr(
                      0, notion_image_url.find("Signature=old")) +
                      "Signature=fresh";
                  expires_at = 4600;
                  return true;
                },
                refreshed_image, image_error) &&
                expired_refreshes == 1 && expired_downloads == 1 &&
                refreshed_image.body == "fresh image",
            "expired Notion image refreshes its block before downloading");

    potion::ImageRegistry rejected_registry;
    const std::string rejected_key = rejected_registry.register_notion_url(
        notion_image_url, notion_block_id, 4600);
    int rejected_downloads = 0, rejected_refreshes = 0;
    require(potion::retrieve_registered_image(
                rejected_registry, rejected_key, 1000,
                [&](const std::string &url, potion::BinaryResponse &result,
                    std::string &error, long *status) {
                  ++rejected_downloads;
                  if (url.find("Signature=old") != std::string::npos) {
                    if (status) *status = 403;
                    error = "Image server returned HTTP 403";
                    return false;
                  }
                  if (status) *status = 200;
                  result = {"image/png", "retried image"};
                  return true;
                },
                [&](const std::string &, std::string &url,
                    std::int64_t &expires_at, std::string &) {
                  ++rejected_refreshes;
                  url = notion_image_url.substr(
                      0, notion_image_url.find("Signature=old")) +
                      "Signature=fresh";
                  expires_at = 4600;
                  return true;
                },
                refreshed_image, image_error) &&
                rejected_downloads == 2 && rejected_refreshes == 1,
            "authorization failure refreshes and retries exactly once");

    potion::ImageRegistry external_registry;
    const std::string external_key = external_registry.register_url(
        "https://example.com/stable.png");
    int external_downloads = 0, external_refreshes = 0;
    require(!potion::retrieve_registered_image(
                external_registry, external_key, 1000,
                [&](const std::string &, potion::BinaryResponse &,
                    std::string &error, long *status) {
                  ++external_downloads;
                  if (status) *status = 403;
                  error = "external failure";
                  return false;
                },
                [&](const std::string &, std::string &, std::int64_t &,
                    std::string &) {
                  ++external_refreshes;
                  return true;
                },
                refreshed_image, image_error) &&
                external_downloads == 1 && external_refreshes == 0,
            "external images never trigger a Notion block refresh");

    char image_cache_directory[] = "/tmp/potion-image-cache.XXXXXX";
    require(::mkdtemp(image_cache_directory) != nullptr,
            "create temporary image cache directory");
    potion::SessionImageCache image_cache(image_cache_directory, 1024 * 1024,
                                           8);
    std::string cache_error;
    require(image_cache.reset(cache_error), "initialize session image cache");
    const std::uint64_t initial_cache_generation = image_cache.generation();
    require(image_cache.store(image_key, "image/png", "cached bytes",
                              initial_cache_generation),
            "store image and MIME type atomically");
    std::string cached_type, cached_body;
    require(image_cache.load(image_key, cached_type, cached_body) &&
                cached_type == "image/png" && cached_body == "cached bytes",
            "session image cache preserves content type and bytes");
    require(image_cache.reset(cache_error) &&
                !image_cache.load(image_key, cached_type, cached_body),
            "startup reset removes previous session image entries");
    const std::uint64_t before_clear = image_cache.generation();
    image_cache.clear();
    require(!image_cache.store(image_key, "image/png", "stale download",
                               before_clear) &&
                !image_cache.load(image_key, cached_type, cached_body),
            "a download started before logout cannot repopulate the cache");

    potion::ImageRegistry cache_hit_registry;
    const std::string cache_hit_key = cache_hit_registry.register_notion_url(
        notion_image_url, notion_block_id, 900);
    const std::uint64_t cache_hit_generation = image_cache.generation();
    require(image_cache.store(cache_hit_key, "image/webp", "offline image",
                              cache_hit_generation),
            "seed expired signed URL cache entry");
    int cache_hit_downloads = 0, cache_hit_refreshes = 0;
    require(potion::retrieve_registered_image(
                cache_hit_registry, cache_hit_key, 5000,
                [&](const std::string &, potion::BinaryResponse &,
                    std::string &, long *) {
                  ++cache_hit_downloads;
                  return false;
                },
                [&](const std::string &, std::string &, std::int64_t &,
                    std::string &) {
                  ++cache_hit_refreshes;
                  return false;
                },
                refreshed_image, image_error, &image_cache) &&
                refreshed_image.content_type == "image/webp" &&
                refreshed_image.body == "offline image" &&
                cache_hit_downloads == 0 && cache_hit_refreshes == 0,
            "cache hit bypasses expired URL refresh and remote download");

    image_cache.clear();
    potion::ImageRegistry cache_miss_registry;
    const std::string cache_miss_key = cache_miss_registry.register_url(
        "https://example.com/cache-miss.png");
    int cache_miss_downloads = 0;
    const auto unused_refresh =
        [](const std::string &, std::string &, std::int64_t &,
           std::string &) { return false; };
    const auto cache_miss_download =
        [&](const std::string &, potion::BinaryResponse &result,
            std::string &, long *status) {
          ++cache_miss_downloads;
          if (status) *status = 200;
          result = {"image/png", "downloaded once"};
          return true;
        };
    require(potion::retrieve_registered_image(
                cache_miss_registry, cache_miss_key, 1000,
                cache_miss_download, unused_refresh, refreshed_image,
                image_error, &image_cache) && cache_miss_downloads == 1,
            "cache miss uses the existing remote retrieval path");
    cache_miss_registry.clear();
    require(potion::retrieve_registered_image(
                cache_miss_registry, cache_miss_key, 1000,
                cache_miss_download, unused_refresh, refreshed_image,
                image_error, &image_cache) && cache_miss_downloads == 1 &&
                refreshed_image.body == "downloaded once",
            "cached image remains available without registry or network");

    std::atomic<int> active_downloads{0}, peak_downloads{0};
    std::atomic<int> serialized_downloads{0};
    const auto serialized_download =
        [&](const std::string &, potion::BinaryResponse &result,
            std::string &, long *status) {
          const int active = active_downloads.fetch_add(1) + 1;
          serialized_downloads.fetch_add(1);
          int peak = peak_downloads.load();
          while (peak < active &&
                 !peak_downloads.compare_exchange_weak(peak, active)) {}
          std::this_thread::sleep_for(std::chrono::milliseconds(40));
          active_downloads.fetch_sub(1);
          if (status) *status = 200;
          result = {"image/png", "serialized"};
          return true;
        };
    auto first_fetch = std::async(std::launch::async, [&] {
      potion::BinaryResponse result;
      std::string error;
      return potion::retrieve_registered_image(
          external_registry, external_key, 1000, serialized_download,
          unused_refresh, result, error, &image_cache);
    });
    auto second_fetch = std::async(std::launch::async, [&] {
      potion::BinaryResponse result;
      std::string error;
      return potion::retrieve_registered_image(
          external_registry, external_key, 1000, serialized_download,
          unused_refresh, result, error, &image_cache);
    });
    require(first_fetch.get() && second_fetch.get() && peak_downloads == 1 &&
                serialized_downloads.load() == 1,
            "duplicate proxy requests share one download and cached result");
    image_cache.clear();
    potion::SessionImageCache bounded_cache(image_cache_directory,
                                             1024 * 1024, 2);
    require(bounded_cache.reset(cache_error), "reset bounded image cache");
    potion::ImageRegistry cache_key_registry;
    const std::string bounded_key_1 = cache_key_registry.register_url(
        "https://example.com/bounded-1.png");
    const std::string bounded_key_2 = cache_key_registry.register_url(
        "https://example.com/bounded-2.png");
    const std::string bounded_key_3 = cache_key_registry.register_url(
        "https://example.com/bounded-3.png");
    const std::uint64_t bounded_generation = bounded_cache.generation();
    require(bounded_cache.store(bounded_key_1, "image/png", "one",
                                bounded_generation) &&
                bounded_cache.store(bounded_key_2, "image/png", "two",
                                    bounded_generation) &&
                bounded_cache.store(bounded_key_3, "image/png", "three",
                                    bounded_generation),
            "bounded image cache accepts new entries");
    const bool bounded_first = bounded_cache.load(
        bounded_key_1, cached_type, cached_body);
    const bool bounded_second = bounded_cache.load(
        bounded_key_2, cached_type, cached_body);
    require(bounded_cache.load(bounded_key_3, cached_type, cached_body) &&
                (!bounded_first || !bounded_second),
            "bounded image cache evicts an older entry");
    bounded_cache.clear();
    require(::rmdir(image_cache_directory) == 0,
            "remove temporary image cache directory");

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
    require(logical_blocks.find(
                "class=\"potion-block math display-math\" "
                "data-potion-expression=\"x^2\"") != std::string::npos,
            "display equation exposes stable expression metadata");
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

    std::vector<std::string> rendered_image_keys;
    const std::string figure = renderer.render(
      "Impulse response $`h[n]`$\n"
      "![Impulse response $`h[n]`$](https://example.com/impulse.png)\n",
      &rendered_image_keys);
    require(figure.find("<img data-src=\"http://127.0.0.1:8766/api/images/") != std::string::npos, "lazy image source");
    require(figure.find("<figcaption>Impulse response <span class=\"math\" data-potion-atomic=\"1\" data-potion-expression=\"h[n]\"><span class=\"katex\">") != std::string::npos,
            "caption math is rendered natively");
    require(figure.find("potion-editable-content\">Impulse response") == std::string::npos && figure.find("<p><figure>") == std::string::npos, "deduplicated standalone figure");
    require(rendered_image_keys.size() == 1 &&
                rendered_image_keys[0] == images.register_url(
                    "https://example.com/impulse.png"),
            "renderer reports page image keys for asynchronous prefetch");
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

    const std::string unsupported_blocks = renderer.render(
      "<synced_block_reference url=\"https://www.notion.so/example\">\n"
      "\t![Supported child](https://example.com/child.png)\n"
      "</synced_block_reference>\n"
      "<bookmark url=\"https://example.com/private-query?token=secret\"/>\n");
    const std::string synced_notice =
      "<div class=\"potion-block unsupported\"><code>synced_block_reference</code> content is not yet supported.</div>";
    require(unsupported_blocks.find(synced_notice) != std::string::npos &&
            unsupported_blocks.find(synced_notice,
                unsupported_blocks.find(synced_notice) + 1) == std::string::npos,
            "unsupported container names its Notion type once");
    require(unsupported_blocks.find("<code>bookmark</code> content is not yet supported.") !=
                std::string::npos &&
            unsupported_blocks.find("token=secret") == std::string::npos,
            "unsupported self-closing block exposes its type without attributes");
    require(unsupported_blocks.find("alt=\"Supported child\"") != std::string::npos,
            "supported children inside an unsupported wrapper still render");

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
    const std::string parenthesized_image_key =
        edge_images.register_url("https://example.com/plot(a).png");
    require(edge_images.resolve(parenthesized_image_key, image_url) &&
                image_url == "https://example.com/plot(a).png",
            "balanced parentheses in image URL");

    potion::ImageRegistry http_images;
    potion::MarkdownRenderer http_renderer(http_images);
    const std::string http_image = http_renderer.render(
      "![HTTP-only image](http://example.com/image.png)\n");
    require(http_image.find("data-src=") == std::string::npos &&
            http_image.find("<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">HTTP-only image</span></p>") != std::string::npos,
            "HTTP image does not register a guaranteed-broken image");
    require(!http_images.resolve("not-registered", image_url),
            "HTTP image registry remains empty");

    const std::string query_page = renderer.render(
      "<page url=\"https://www.notion.so/Books-5908cc548ef342b6b84e254fa1785a21?pvs=4\">Query page</page>\n"
      "<page url=\"https://www.notion.so/Books-5908cc548ef342b6b84e254fa1785a21#fragment\">Fragment page</page>\n");
    require(query_page.find("data-page-id=\"5908cc548ef342b6b84e254fa1785a21\"") != std::string::npos,
            "page ID survives query or fragment");
    require(query_page.find("Fragment page") != std::string::npos, "fragment page rendered");
    const std::string inline_notion_page = renderer.render(
      "Read [Morbi](/p/5908cc548ef342b6b84e254fa1785a21?pvs=25) now.\n");
    require(inline_notion_page.find(
                "<a class=\"notion-page-link\" href=\"https://www.notion.so/p/5908cc548ef342b6b84e254fa1785a21?pvs=25\" data-page-id=\"5908cc548ef342b6b84e254fa1785a21\">Morbi</a>") != std::string::npos,
            "relative inline Notion page link remains visible and opens inside Potion");
    require(inline_notion_page.find("data-block-id=") == std::string::npos,
            "ordinary Notion page link has no block target");
    const std::string inline_notion_block = renderer.render(
      "[This link](/p/5908CC548EF342B6B84E254FA1785A21?pvs=25#"
      "3E0D2870-A152-8002-BC7A-FD8BF9B021A0)\n");
    require(inline_notion_block.find(
                "data-page-id=\"5908cc548ef342b6b84e254fa1785a21\"") !=
                std::string::npos &&
            inline_notion_block.find(
                "data-block-id=\"3e0d2870a1528002bc7afd8bf9b021a0\"") !=
                std::string::npos,
            "hyphenated block fragment and page ID are normalized");
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
      "Left-to-right paragraph.\n"
      "Potion supports متن فارسی inside English.\n"
      "این یک متن فارسی درباره Potion و HTTP است.\n"
      "2026: این متن نیز راست به چپ است.\n"
      "⚠️ <span underline=\"true\">این متن نیز راست به چپ است.</span>\n");
    require(rtl.find("<p class=\"potion-block potion-editable\" dir=\"rtl\"><span class=\"potion-editable-content\">سلام عرض میکنم خدمت شما!</span></p>") != std::string::npos,
            "right-to-left paragraph direction");
    require(rtl.find("dir=\"rtl\">Left-to-right") == std::string::npos,
            "left-to-right paragraph direction unchanged");
    require(rtl.find("dir=\"rtl\"><span class=\"potion-editable-content\">Potion supports") == std::string::npos,
            "English-leading mixed paragraph remains left-to-right");
    require(rtl.find("dir=\"rtl\"><span class=\"potion-editable-content\">این یک متن فارسی درباره Potion و HTTP است.</span>") != std::string::npos,
            "Persian-leading mixed paragraph is right-to-left");
    require(rtl.find("dir=\"rtl\"><span class=\"potion-editable-content\">2026: این متن نیز راست به چپ است.</span>") != std::string::npos,
            "leading weak digits do not override the first strong Persian character");
    require(rtl.find("dir=\"rtl\"><span class=\"potion-editable-content\">⚠") != std::string::npos &&
            rtl.find("<u>این متن نیز راست به چپ است.</u>") != std::string::npos,
            "leading emoji and inline markup do not override the first strong Persian character");

    const std::string highlighted = renderer.render(
      "<span color=\"yellow_bg\">Highlighted text</span>\n"
      "A highlighted paragraph {color=\"blue_background\"}\n");
    require(highlighted.find("notion-color-yellow-bg") != std::string::npos, "inline highlight");
    require(highlighted.find("notion-color-blue-bg") != std::string::npos, "block highlight");
    require(highlighted.find("Unsupported Notion content") == std::string::npos, "highlight supported");
    const std::string highlighted_equation_html =
        renderer.render(colored_equation_markdown);
    require(highlighted_equation_html.find(
                "<span class=\"notion-color notion-color-blue\"><span class=\"math\"") !=
                std::string::npos &&
            highlighted_equation_html.find(
                "<span class=\"notion-color notion-color-yellow-bg\"><u><span class=\"math\"") !=
                std::string::npos,
            "inline equation annotation markup is rendered around native math");
    const std::string inline_only = renderer.render(
      "Plain <span color=\"orange\">orange words</span> remain plain\n");
    require(inline_only.find("<p class=\"potion-block potion-editable\"><span class=\"potion-editable-content\">Plain <span class=\"notion-color notion-color-orange\">") != std::string::npos, "inline color scope");
    const std::string combined_inline = renderer.render(
      "<span color=\"yellow\" underline=\"true\">***lacinia***</span>\n");
    require(combined_inline.find(
                "<span class=\"notion-color notion-color-yellow\"><u><strong><em>lacinia</em></strong></u></span>") !=
                std::string::npos && combined_inline.find("*lacinia") == std::string::npos,
            "combined bold italic underline and color formatting is nested correctly");
    require(inline_only.find("<p class=\"potion-block potion-editable notion-color") == std::string::npos, "inline color did not leak to block");

    const std::string emoji = renderer.render("Emoji 😀 📚 🚀 ⚠️ ❤️ ✅\n");
    require(emoji.find("😀 📚 🚀 ⚠<span class=\"emoji-variation-selector\">️</span> ") !=
                std::string::npos &&
            emoji.find("❤<span class=\"emoji-variation-selector\">️</span> ✅") !=
                std::string::npos,
            "emoji variation selectors stay logical but are hidden from old Mesquite");

    char directory[] = "/tmp/potion-test.XXXXXX";
    require(::mkdtemp(directory) != nullptr, "mkdtemp");

    const std::string page_cache_unit_dir =
        std::string(directory) + "/page-cache-unit";
    require(::mkdir(page_cache_unit_dir.c_str(), 0700) == 0,
            "create temporary Pages cache directory");
    { std::ofstream stale(page_cache_unit_dir + "/pages-stale.json");
      stale << "old session"; }
    potion::SessionPageCache page_cache(page_cache_unit_dir, 2, 64 * 1024);
    require(page_cache.reset(cache_error) &&
                ::access((page_cache_unit_dir + "/pages-stale.json").c_str(),
                         F_OK) != 0,
            "Pages cache startup reset removes prior-session snapshots");
    int snapshot_builds = 0;
    const auto build_snapshot = [&](std::string &error) {
      ++snapshot_builds;
      error.clear();
      return std::vector<potion::PageSummary>{
          {"5908cc548ef342b6b84e254fa1785a21",
           "Snapshot " + std::to_string(snapshot_builds), {},
           "2026-09-27T00:00:00.000Z"}};
    };
    std::vector<potion::PageSummary> snapshot;
    require(page_cache.get_or_build("", false, build_snapshot, snapshot,
                                    cache_error) &&
                snapshot_builds == 1 && snapshot[0].title == "Snapshot 1" &&
                ::access((page_cache_unit_dir + "/pages-1.json").c_str(),
                         F_OK) == 0,
            "Pages cache stores the first normalized session snapshot");
    require(page_cache.get_or_build("", false, build_snapshot, snapshot,
                                    cache_error) &&
                snapshot_builds == 1 && snapshot[0].title == "Snapshot 1",
            "Pages cache hit does not rebuild the Notion search");
    require(page_cache.get_or_build("different", false, build_snapshot,
                                    snapshot, cache_error) &&
                snapshot_builds == 2 && snapshot[0].title == "Snapshot 2",
            "different search queries use separate snapshots");
    require(page_cache.get_or_build("", true, build_snapshot, snapshot,
                                    cache_error) &&
                snapshot_builds == 3 && snapshot[0].title == "Snapshot 3",
            "explicit refresh atomically replaces one query snapshot");
    const auto failed_refresh = [&](std::string &error) {
      error = "refresh failed";
      return std::vector<potion::PageSummary>{};
    };
    require(!page_cache.get_or_build("", true, failed_refresh, snapshot,
                                     cache_error) &&
                page_cache.get_or_build("", false, build_snapshot, snapshot,
                                        cache_error) &&
                snapshot_builds == 3 && snapshot[0].title == "Snapshot 3",
            "failed refresh leaves the last good snapshot available");
    page_cache.clear();
    require(::access((page_cache_unit_dir + "/pages-2.json").c_str(), F_OK) != 0 &&
                ::access((page_cache_unit_dir + "/pages-3.json").c_str(), F_OK) != 0,
            "Pages cache clear removes all query snapshots");
    require(::rmdir(page_cache_unit_dir.c_str()) == 0,
            "remove temporary Pages cache directory");

    {
      const std::string server_cache = std::string(directory) + "/image-cache";
      require(::mkdir(server_cache.c_str(), 0700) == 0,
              "create stale server cache directory");
      const std::string stale_cache_file = server_cache + "/stale.tmp";
      { std::ofstream output(stale_cache_file); output << "old session"; }
      const std::string server_page_cache =
          std::string(directory) + "/page-cache";
      require(::mkdir(server_page_cache.c_str(), 0700) == 0,
              "create stale server Pages cache directory");
      const std::string stale_page_cache_file =
          server_page_cache + "/pages-stale.json";
      { std::ofstream output(stale_page_cache_file); output << "old session"; }
      const std::string first_url = "https://example.com/prefetch-first.png";
      const std::string second_url = "https://example.com/prefetch-second.png";
      potion::ImageRegistry key_registry;
      const std::string first_key = key_registry.register_url(first_url);
      const std::string second_key = key_registry.register_url(second_url);
      std::atomic<int> downloads{0}, completed_downloads{0};
      std::atomic<int> page_searches{0};
      std::atomic<int> page_retrievals{0};
      std::atomic<bool> first_download_started{false};
      RunningServer running(
          directory, {},
          [](const std::string &, std::string &) { return true; },
          [&](potion::ServerOptions &options) {
            options.page_retriever =
                [&](const std::string &, const std::string &page_id,
                    potion::PageDocument &page, std::string &) {
                  page_retrievals.fetch_add(1);
                  page.id = page_id;
                  page.title = "Prefetch test";
                  page.markdown = "![First](" + first_url + ")\n" +
                                  "![Second](" + second_url + ")\n";
                  return true;
                };
            options.image_downloader =
                [&](const std::string &url, potion::BinaryResponse &result,
                    std::string &, long *status) {
                  downloads.fetch_add(1);
                  if (url == first_url) {
                    first_download_started.store(true);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                  }
                  if (status) *status = 200;
                  result = {url == first_url ? "image/png" : "image/webp",
                            url == first_url ? "first image" : "second image"};
                  completed_downloads.fetch_add(1);
                  return true;
                };
            options.image_refresher = unused_refresh;
            options.page_searcher =
                [&](const std::string &, const std::string &query,
                    std::string &) {
                  const int search = page_searches.fetch_add(1) + 1;
                  const std::size_t count = query == "alpha" ? 3 : 25;
                  std::vector<potion::PageSummary> pages;
                  for (std::size_t i = 0; i < count; ++i) {
                    potion::PageUuid uuid;
                    uuid.bytes[12] = static_cast<std::uint8_t>(i >> 24);
                    uuid.bytes[13] = static_cast<std::uint8_t>(i >> 16);
                    uuid.bytes[14] = static_cast<std::uint8_t>(i >> 8);
                    uuid.bytes[15] = static_cast<std::uint8_t>(i);
                    pages.push_back({
                        potion::format_page_uuid(uuid),
                        query + " page " + std::to_string(i) +
                            " search " + std::to_string(search),
                        {}, "2026-09-" +
                            std::to_string(27 - static_cast<int>(i % 20)) +
                            "T00:00:00.000Z"});
                  }
                  return pages;
                };
          });
      const std::string page_cache_auth = "token=page-cache-test-token";
      request(running.server.bound_port(), "/api/auth/token", &page_cache_auth);
      require(::access(stale_cache_file.c_str(), F_OK) != 0,
              "server startup clears stale image cache files");
      require(::access(stale_page_cache_file.c_str(), F_OK) != 0,
              "server startup clears stale Pages cache files");
      const std::string page_id = "5908cc548ef342b6b84e254fa1785a21";
      const auto page_started = std::chrono::steady_clock::now();
      const auto page_response = raw_request(
          running.server.bound_port(), "/api/pages/" + page_id);
      const auto page_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - page_started).count();
      require(page_response.status == 200 && page_elapsed < 400,
              "page response does not wait for image prefetch");
      require(raw_request(running.server.bound_port(),
                          "/api/pages/" + page_id).status == 200 &&
                  page_retrievals.load() == 2,
              "individual Notion page contents remain completely uncached");
      for (int i = 0; i < 100 && !first_download_started.load(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      require(first_download_started.load(),
              "page images begin prefetching asynchronously");
      const auto demand_started = std::chrono::steady_clock::now();
      const auto demanded_second = raw_request(
          running.server.bound_port(), "/api/images/" + second_key);
      const auto demand_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - demand_started).count();
      require(demanded_second.status == 200 &&
                  demanded_second.body == "second image" &&
                  demanded_second.headers.find("Content-Type: image/webp") !=
                      std::string::npos &&
                  demand_elapsed < 400,
              "direct image demand bypasses the pending prefetch queue");
      for (int i = 0; i < 200 && completed_downloads.load() < 2; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      require(completed_downloads.load() == 2,
              "background prefetch completes through the normal image path");
      const auto cached_first = raw_request(
          running.server.bound_port(), "/api/images/" + first_key);
      const auto cached_second = raw_request(
          running.server.bound_port(), "/api/images/" + second_key);
      require(cached_first.status == 200 && cached_first.body == "first image" &&
                  cached_second.status == 200 &&
                  cached_second.body == "second image" &&
                  downloads.load() == 2,
              "prefetched and demanded images are reused from session cache");
      const auto first_pages = potion::Json::parse(request(
          running.server.bound_port(), "/api/pages?offset=0&pageSize=10"));
      const auto second_pages = potion::Json::parse(request(
          running.server.bound_port(), "/api/pages?offset=10&pageSize=10"));
      require(page_searches.load() == 1 &&
                  first_pages.get("pages").items().size() == 10 &&
                  first_pages.get("total").number() == 25 &&
                  first_pages.get("hasMore").boolean() &&
                  second_pages.get("pages").items().size() == 10 &&
                  second_pages.get("offset").number() == 10,
              "backend pagination reuses one Notion Pages snapshot");
      const auto query_pages = potion::Json::parse(request(
          running.server.bound_port(),
          "/api/pages?query=alpha&offset=0&pageSize=10"));
      require(page_searches.load() == 2 &&
                  query_pages.get("total").number() == 3,
              "search query builds an isolated snapshot");
      request(running.server.bound_port(),
              "/api/pages?offset=20&pageSize=10");
      require(page_searches.load() == 2,
              "returning to an existing query reuses its snapshot");
      const auto refreshed_pages = potion::Json::parse(request(
          running.server.bound_port(),
          "/api/pages?offset=0&pageSize=10&refresh=1"));
      require(page_searches.load() == 3 &&
                  refreshed_pages.get("pages").items()[0]
                      .get("title").string().find("search 3") !=
                      std::string::npos,
              "explicit refresh rebuilds the active query snapshot");
      require(::access((server_page_cache + "/pages-1.json").c_str(), F_OK) == 0 ||
                  ::access((server_page_cache + "/pages-3.json").c_str(), F_OK) == 0,
              "backend Pages snapshot is stored under the session cache");
      const std::string empty_logout;
      request(running.server.bound_port(), "/api/auth/logout", &empty_logout);
      require(::access((server_cache + "/" + first_key + ".cache").c_str(),
                       F_OK) != 0 &&
                  ::access((server_cache + "/" + second_key + ".cache").c_str(),
                           F_OK) != 0,
              "logout clears cached image files");
      require(::access((server_page_cache + "/pages-1.json").c_str(), F_OK) != 0 &&
                  ::access((server_page_cache + "/pages-2.json").c_str(), F_OK) != 0 &&
                  ::access((server_page_cache + "/pages-3.json").c_str(), F_OK) != 0,
              "logout clears cached Pages snapshots");
    }

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
      require(!state.settings().bionic_reading,
              "Bionic Reading defaults off for existing settings files");
      require(state.settings().word_spacing == "normal" &&
                  state.settings().line_spacing == "normal",
              "reader spacing defaults normal for existing settings files");
      require(state.set_setting("cardFont", "Palatino", error), "save setting");
      require(state.set_setting("bionicReading", "1", error),
              "save Bionic Reading setting");
      require(state.settings().bionic_reading &&
                  state.settings_json().find("\"bionicReading\":true") !=
                      std::string::npos,
              "Bionic Reading setting JSON");
      error.clear();
      require(!state.set_setting("bionicReading", "experimental", error),
              "reject invalid Bionic Reading setting");
      error.clear();
      require(state.settings().code_size == 18,
              "code size defaults to 18 for existing settings files");
      require(state.set_setting("codeSize", "20", error),
              "save code size");
      require(state.settings().code_size == 20 &&
                  state.settings_json().find("\"codeSize\":20") !=
                      std::string::npos,
              "code size JSON");
      require(state.set_setting("codeSize", "14", error) &&
                  state.set_setting("codeSize", "30", error),
              "save code size grid limits");
      require(state.settings().code_size == 30, "code size maximum kept");
      error.clear();
      require(!state.set_setting("codeSize", "12", error),
              "reject code size below minimum");
      error.clear();
      require(!state.set_setting("codeSize", "32", error),
              "reject code size above maximum");
      error.clear();
      require(!state.set_setting("codeSize", "19", error),
              "reject code size off the 2px grid");
      error.clear();
      require(state.settings().code_size == 30,
              "rejected code sizes leave the setting unchanged");
      require(state.set_setting("wordSpacing", "plus", error),
              "save word spacing");
      require(state.set_setting("lineSpacing", "plusplus", error),
              "save line spacing");
      require(state.settings_json().find("\"wordSpacing\":\"plus\"") !=
                  std::string::npos &&
                  state.settings_json().find("\"lineSpacing\":\"plusplus\"") !=
                  std::string::npos,
              "spacing settings JSON");
      error.clear();
      require(!state.set_setting("wordSpacing", "wide", error),
              "reject invalid word spacing");
      error.clear();
      require(!state.set_setting("lineSpacing", "double", error),
              "reject invalid line spacing");
      error.clear();
      require(state.set_setting("wordSpacing", "plusplusplus", error) &&
                  state.set_setting("lineSpacing", "plusplusplus", error),
              "save maximum reader spacing");
      require(state.set_setting("nightPageMode", "palette-images", error), "save night page mode");
      require(state.settings_json().find("\"nightPageMode\":\"palette-images\"") != std::string::npos, "night page mode JSON");
      require(state.set_setting("pageButtonMode", "reversed", error), "save page button mode");
      require(state.settings_json().find("\"pageButtonMode\":\"reversed\"") != std::string::npos, "page button mode JSON");
      require(state.settings().page_sort_mode == "opened", "opened sort is default");
      require(state.settings().rotation_mode == "auto", "auto rotation is default");
      require(state.set_setting("pageSortMode", "edited", error), "save page sort mode");
      require(state.settings_json().find("\"pageSortMode\":\"edited\"") != std::string::npos,
              "page sort mode JSON");
      error.clear();
      require(!state.set_setting("pageSortMode", "random", error), "reject invalid page sort mode");
      error.clear();
      require(state.set_setting("rotationMode", "locked", error), "save rotation mode");
      require(state.settings_json().find("\"rotationMode\":\"locked\"") != std::string::npos,
              "rotation mode JSON");
      error.clear();
      require(!state.set_setting("rotationMode", "sideways", error),
              "reject invalid rotation mode");
      error.clear();
      require(state.set_page_pinned("5908cc548ef342b6b84e254fa1785a21", true, error),
              "persist page pin");
      require(state.set_page_pinned("5908cc54-8ef3-42b6-b84e-254fa1785a21", true, error),
              "updating page pin does not duplicate it");
      require(state.page_pinned("5908cc548ef342b6b84e254fa1785a21"), "page is pinned");
      require(::stat((std::string(directory) + "/pins.conf").c_str(), &info) == 0 &&
                  (info.st_mode & 0777) == 0600,
              "page pin file permissions");
    }
    {
      potion::AppState reloaded(directory);
      require(reloaded.token() == "test-token", "reload token");
      require(reloaded.settings().page_sort_mode == "edited", "reload page sort mode");
      require(reloaded.settings().rotation_mode == "locked", "reload rotation mode");
      require(reloaded.settings().bionic_reading,
              "reload Bionic Reading setting");
      require(reloaded.settings().code_size == 30,
              "reload code size");
      require(reloaded.settings().word_spacing == "plusplusplus" &&
                  reloaded.settings().line_spacing == "plusplusplus",
              "reload reader spacing settings");
      require(reloaded.page_pinned("5908cc54-8ef3-42b6-b84e-254fa1785a21"),
              "reload normalized page pin");
    }

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
      RunningServer running(directory, "0123456789abcdef0123456789abcdef");
      const unsigned port = running.server.bound_port();
      const std::string no_store = "Cache-Control: no-store\r\n";
      const std::string immutable =
          "Cache-Control: public, max-age=31536000, immutable\r\n";
      require(std::string(potion::cache_control_value(
                  potion::CachePolicy::proxied_image)) ==
                  "private, max-age=604800",
              "successful proxied images use the seven-day private policy");
      require(std::string(potion::cache_control_value(
                  potion::CachePolicy::immutable_asset)) ==
                  "public, max-age=31536000, immutable",
              "immutable asset cache policy");

      require(raw_request(port, "/").headers.find(no_store) != std::string::npos,
              "index.html is no-store");
      require(raw_request(port, "/app.js").headers.find(no_store) != std::string::npos,
              "app.js is no-store");
      require(raw_request(port, "/app.css").headers.find(no_store) != std::string::npos,
              "app.css is no-store");
      require(raw_request(port, "/potion_logo.png").headers.find(no_store) !=
                  std::string::npos,
              "ordinary UI images are no-store");
      require(raw_request(port, "/vendor/katex/katex.min.css").headers.find(immutable) !=
                  std::string::npos,
              "KaTeX vendor assets use the immutable policy");
      require(raw_request(port,
                  "/vendor/katex/fonts/KaTeX_Main-Regular.woff").headers.find(immutable) !=
                  std::string::npos,
              "bundled WOFF fonts use the immutable policy");
      const auto bionic_font = raw_request(
          port, "/vendor/fast-font/fonts/PotionFastSans-Regular.otf");
      require(bionic_font.status == 200 &&
                  bionic_font.headers.find("Content-Type: font/otf") !=
                      std::string::npos &&
                  bionic_font.headers.find(immutable) != std::string::npos,
              "bundled Bionic Reading OTF uses the immutable font policy");
      const auto emoji_font = raw_request(
          port, "/vendor/noto-emoji/fonts/NotoEmoji-Regular.ttf");
      require(emoji_font.status == 200 &&
                  emoji_font.headers.find("Content-Type: font/ttf") !=
                      std::string::npos &&
                  emoji_font.headers.find(immutable) != std::string::npos,
              "bundled static Noto Emoji TTF uses the immutable font policy");
      require(raw_request(port, "/api/status").headers.find(no_store) !=
                  std::string::npos,
              "dynamic API responses are no-store");
      const auto unknown_image = raw_request(
          port, "/api/images/ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
      require(unknown_image.status == 404 &&
                  unknown_image.headers.find(no_store) != std::string::npos,
              "unknown image keys return a non-cacheable 404");

      require(potion::Json::parse(request(port, "/api/status")).get("startPageId").string() ==
                  "0123456789abcdef0123456789abcdef",
              "startup page status");
      const std::string page_id = "5908cc548ef342b6b84e254fa1785a21";
      const std::string pin_page = "pinned=1";
      require(potion::Json::parse(request(port, "/api/pages/" + page_id + "/pin", &pin_page))
                  .get("pinned").boolean(), "page pin API");
      require(potion::AppState(directory).page_pinned(page_id), "page pin API persists");
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
      require(!potion::AppState(directory).page_pinned(page_id), "logout clears page pins");
      require(::access((std::string(directory) + "/pins.conf").c_str(), F_OK) != 0,
              "logout removes persisted page pins");

    }
    {
      RunningServer running(
          directory, {},
          [](const std::string &token, std::string &error) {
            if (token == "valid-test-token") return true;
            error = "Notion rejected the test token";
            return false;
          });
      const unsigned main_port = running.server.bound_port();
      std::string setup_url;
      for (int i = 0; i < 100 && setup_url.empty(); ++i) {
        setup_url = potion::Json::parse(request(main_port, "/api/status"))
                        .get("remoteSetupUrl").string();
        if (setup_url.empty())
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      require(setup_url.compare(0, 7, "http://") == 0,
              "logged-out status advertises temporary remote setup URL");
      const auto path_at = setup_url.find('/', 7);
      const auto colon = setup_url.rfind(':', path_at);
      require(path_at != std::string::npos && colon != std::string::npos,
              "remote setup URL contains a port and randomized path");
      const unsigned setup_port =
          static_cast<unsigned>(std::stoul(setup_url.substr(
              colon + 1, path_at - colon - 1)));
      const std::string setup_path = setup_url.substr(path_at);
      require(setup_path == "/",
              "remote setup URL stays short enough to type on another device");
      const auto setup_page = raw_request(setup_port, setup_path);
      require(setup_page.status == 200 &&
                  setup_page.body.find("name=\"token\"") != std::string::npos &&
                  setup_page.body.find("/api/") == std::string::npos,
              "remote listener exposes only the minimal token form");
      require(raw_request(setup_port, "/api/status").status == 404,
              "remote listener does not expose Potion APIs");
      const std::string rejected_token = "token=invalid";
      const auto rejected = raw_request(setup_port, setup_path, &rejected_token);
      require(rejected.status == 400 &&
                  rejected.body.find("Notion rejected the test token") !=
                      std::string::npos,
              "invalid remote token keeps setup available with an error");
      const std::string valid_token = "token=valid-test-token";
      require(raw_request(setup_port, setup_path, &valid_token).status == 200,
              "valid remote token is accepted");
      for (int i = 0; i < 100; ++i) {
        const auto status = potion::Json::parse(request(main_port, "/api/status"));
        if (status.get("authenticated").boolean() &&
            status.get("remoteSetupUrl").is_null())
          break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      const auto authenticated =
          potion::Json::parse(request(main_port, "/api/status"));
      require(authenticated.get("authenticated").boolean() &&
                  authenticated.get("remoteSetupUrl").is_null(),
              "successful login removes the remote setup URL");
      bool setup_closed = false;
      for (int i = 0; i < 100 && !setup_closed; ++i) {
        try {
          raw_request(setup_port, setup_path);
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } catch (...) { setup_closed = true; }
      }
      require(setup_closed,
              "successful login closes the externally visible listener");
    }
    ::unlink((std::string(directory) + "/token").c_str());
    ::unlink((std::string(directory) + "/state.conf").c_str());
    ::unlink((std::string(directory) + "/pins.conf").c_str());
    const std::string position_test_dirs[] = {
      positions_dir, malformed_dir, expiry_dir, cap_dir
    };
    for (const auto &path : position_test_dirs) {
      ::unlink((path + "/positions.dat").c_str());
      ::unlink((path + "/positions.dat.tmp").c_str());
      ::rmdir(path.c_str());
    }
    ::rmdir((std::string(directory) + "/image-cache").c_str());
    ::rmdir((std::string(directory) + "/page-cache").c_str());
    ::rmdir(directory);
    std::cout << "Potion tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Potion test failure: " << error.what() << '\n';
    return 1;
  }
}
