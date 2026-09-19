#include "potion/notion_client.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace potion {
namespace {
std::once_flag curl_once;
size_t write_body(char *data, size_t size, size_t count, void *opaque) { auto *body = static_cast<std::string *>(opaque); body->append(data, size * count); return size * count; }
std::string title_of(const Json &page) {
  for (const auto &property : page.get("properties").members()) {
    if (property.second.get("type").string() != "title") continue;
    std::string title; for (const auto &part : property.second.get("title").items()) title += part.get("plain_text").string();
    if (!title.empty()) return title;
  }
  return "Untitled";
}
struct EditableBlock {
  std::string id, type, plain_text;
  Json rich_text;
};
std::size_t utf16_length(const std::string &text) {
  std::size_t units = 0;
  for (std::size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    std::size_t bytes = 1;
    unsigned code = c;
    if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) { bytes = 2; code = c & 0x1f; }
    else if ((c & 0xf0) == 0xe0 && i + 2 < text.size()) { bytes = 3; code = c & 0x0f; }
    else if ((c & 0xf8) == 0xf0 && i + 3 < text.size()) { bytes = 4; code = c & 0x07; }
    for (std::size_t j = 1; j < bytes; ++j)
      code = (code << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3f);
    units += code > 0xffff ? 2 : 1;
    i += bytes;
  }
  return units;
}
bool utf16_byte(const std::string &text, std::size_t wanted, std::size_t &byte) {
  std::size_t units = 0;
  for (std::size_t i = 0; i < text.size();) {
    if (units == wanted) { byte = i; return true; }
    const unsigned char c = static_cast<unsigned char>(text[i]);
    std::size_t bytes = 1;
    unsigned code = c;
    if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) { bytes = 2; code = c & 0x1f; }
    else if ((c & 0xf0) == 0xe0 && i + 2 < text.size()) { bytes = 3; code = c & 0x0f; }
    else if ((c & 0xf8) == 0xf0 && i + 3 < text.size()) { bytes = 4; code = c & 0x07; }
    for (std::size_t j = 1; j < bytes; ++j)
      code = (code << 6) | (static_cast<unsigned char>(text[i + j]) & 0x3f);
    const std::size_t width = code > 0xffff ? 2 : 1;
    if (units + width > wanted) return false;
    units += width;
    i += bytes;
  }
  if (units == wanted) { byte = text.size(); return true; }
  return false;
}
Json writable_text(const Json &source, const std::string &content,
                   const std::string &format, bool selected, bool apply) {
  Json item = Json::object(), text = source.get("text"), annotations = source.get("annotations");
  item["type"] = Json(std::string("text"));
  text["content"] = Json(content);
  Json link = text.get("link");
  if (!link.is_null()) {
    const std::string url = link.get("url").string();
    if (!url.empty() && url.front() == '/') {
      link["url"] = Json(std::string("https://www.notion.so") + url);
      text["link"] = link;
    }
  }
  item["text"] = text;
  if (selected && format == "clear") {
    annotations["bold"] = Json(false);
    annotations["italic"] = Json(false);
    annotations["strikethrough"] = Json(false);
    annotations["underline"] = Json(false);
    annotations["color"] = Json(std::string("default"));
  } else if (selected && format == "bold") {
    annotations["bold"] = Json(apply);
  } else if (selected && format == "underline") {
    annotations["underline"] = Json(apply);
  } else if (selected && format == "highlight") {
    annotations["color"] = Json(std::string(apply ? "yellow_background" : "default"));
  }
  item["annotations"] = annotations;
  return item;
}
Json writable_non_text(const Json &source) {
  const std::string type = source.get("type").string();
  Json item = Json::object();
  item["type"] = Json(type);
  if (type == "equation") {
    Json equation = Json::object();
    equation["expression"] = source.get("equation").get("expression");
    item["equation"] = equation;
  } else if (type == "mention") {
    const Json &source_mention = source.get("mention");
    const std::string mention_type = source_mention.get("type").string();
    Json mention = Json::object();
    mention["type"] = Json(mention_type);
    if (mention_type == "user" || mention_type == "page" ||
        mention_type == "database" || mention_type == "data_source" ||
        mention_type == "agent" || mention_type == "custom_emoji") {
      Json reference = Json::object();
      reference["id"] = source_mention.get(mention_type).get("id");
      mention[mention_type] = reference;
    } else {
      mention[mention_type] = source_mention.get(mention_type);
    }
    item["mention"] = mention;
  } else {
    item[type] = source.get(type);
  }
  item["annotations"] = source.get("annotations");
  return item;
}
Json writable_formatted_equation(const Json &source,
                                 const std::string &format, bool apply) {
  Json item = Json::object();
  Json annotations = source.get("annotations");
  item["type"] = Json(std::string("equation"));
  item["equation"] = source.get("equation");
  if (format == "clear") {
    annotations["bold"] = Json(false);
    annotations["italic"] = Json(false);
    annotations["strikethrough"] = Json(false);
    annotations["underline"] = Json(false);
    annotations["color"] = Json(std::string("default"));
  } else if (format == "underline") {
    annotations["underline"] = Json(apply);
  } else if (format == "highlight") {
    annotations["color"] =
        Json(std::string(apply ? "yellow_background" : "default"));
  }
  item["annotations"] = annotations;
  return item;
}
const std::string &atomic_rich_text_marker() {
  static const std::string marker("\xef\xbf\xbc");
  return marker;
}
std::string logical_rich_text(const Json &source) {
  std::string text;
  for (const auto &part : source.items())
    text += part.get("type").string() == "text" ?
        part.get("plain_text").string() : atomic_rich_text_marker();
  return text;
}
bool xml_attribute(const std::string &tag, const std::string &name,
                   std::string &value) {
  std::size_t at = 0;
  while ((at = tag.find(name, at)) != std::string::npos) {
    const std::size_t after = at + name.size();
    const bool left_boundary = at == 0 || tag[at - 1] == '<' ||
        std::isspace(static_cast<unsigned char>(tag[at - 1]));
    const bool right_boundary = after == tag.size() || tag[after] == '=' ||
        std::isspace(static_cast<unsigned char>(tag[after]));
    if (!left_boundary || !right_boundary) { at = after; continue; }
    std::size_t equals = after;
    while (equals < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[equals]))) ++equals;
    if (equals >= tag.size() || tag[equals] != '=') { at = after; continue; }
    std::size_t start = equals + 1;
    while (start < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[start]))) ++start;
    if (start >= tag.size() || (tag[start] != '"' && tag[start] != '\''))
      return true;
    const char quote = tag[start++];
    const std::size_t end = tag.find(quote, start);
    if (end == std::string::npos) return true;
    value = tag.substr(start, end - start);
    return true;
  }
  return false;
}
void add_svg_viewbox_dimensions(std::string &body) {
  const std::size_t svg = body.find("<svg");
  if (svg == std::string::npos) return;
  const std::size_t end = body.find('>', svg + 4);
  if (end == std::string::npos) return;
  const std::string tag = body.substr(svg, end - svg + 1);
  std::string ignored, viewbox;
  if (xml_attribute(tag, "width", ignored) ||
      xml_attribute(tag, "height", ignored) ||
      !xml_attribute(tag, "viewBox", viewbox)) return;
  std::istringstream values(viewbox);
  double x = 0, y = 0, width = 0, height = 0, extra = 0;
  if (!(values >> x >> y >> width >> height) || values >> extra ||
      !std::isfinite(width) || !std::isfinite(height) ||
      width <= 0 || height <= 0 || width > 10000000 || height > 10000000)
    return;
  std::ostringstream dimensions;
  dimensions.precision(12);
  dimensions << " width=\"" << width << "\" height=\"" << height << "\"";
  const std::size_t insertion = end > svg && body[end - 1] == '/' ? end - 1 : end;
  body.insert(insertion, dimensions.str());
}

std::int64_t days_from_civil(int year, unsigned month, unsigned day) {
  year -= month <= 2;
  const int era = (year >= 0 ? year : year - 399) / 400;
  const unsigned year_of_era = static_cast<unsigned>(year - era * 400);
  const int adjusted_month = static_cast<int>(month) + (month > 2 ? -3 : 9);
  const unsigned day_of_year =
      (153 * static_cast<unsigned>(adjusted_month) + 2) / 5 + day - 1;
  const unsigned day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return static_cast<std::int64_t>(era) * 146097 + day_of_era - 719468;
}

std::int64_t iso8601_timestamp(const std::string &value) {
  int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
  if (std::sscanf(value.c_str(), "%d-%d-%dT%d:%d:%dZ", &year, &month, &day,
                  &hour, &minute, &second) != 6 ||
      month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 ||
      hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60)
    return 0;
  return days_from_civil(year, static_cast<unsigned>(month),
                         static_cast<unsigned>(day)) * 86400 +
         hour * 3600 + minute * 60 + second;
}
}

void NotionClient::add_svg_intrinsic_dimensions(std::string &body) {
  add_svg_viewbox_dimensions(body);
}

NotionClient::NotionClient(std::string api_version, std::string ca_bundle)
    : api_version_(std::move(api_version)), ca_bundle_(std::move(ca_bundle)) { std::call_once(curl_once, [] { if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("Unable to initialize libcurl"); }); }

static NotionClient::Response perform_request(const std::string &url,
    const std::vector<std::string> &headers, const std::string &ca_bundle,
    const std::string *post = nullptr, std::size_t limit = 8 * 1024 * 1024,
    const char *method = nullptr) {
  CURL *curl = curl_easy_init(); if (!curl) throw std::runtime_error("Unable to initialize HTTPS client");
  NotionClient::Response response; curl_slist *list = nullptr;
  for (const auto &header : headers) list = curl_slist_append(list, header.c_str());
  char error[CURL_ERROR_SIZE]{};
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str()); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body); curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error); curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
  if (!ca_bundle.empty()) curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Potion/0.1 Kindle client");
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https"); curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS); curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
  if (post) { if (method) curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method); else curl_easy_setopt(curl, CURLOPT_POST, 1L); curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post->c_str()); curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post->size())); }
  const CURLcode result = curl_easy_perform(curl); curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
  char *content_type = nullptr; curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type); if (content_type) response.content_type = content_type;
  curl_slist_free_all(list); curl_easy_cleanup(curl);
  if (result != CURLE_OK) throw std::runtime_error(error[0] ? error : curl_easy_strerror(result));
  if (response.body.size() > limit) throw std::runtime_error("Notion response is too large for this Kindle");
  return response;
}

NotionClient::Response NotionClient::api_get(const std::string &token, const std::string &path) const { return perform_request("https://api.notion.com" + path, {"Authorization: Bearer " + token, "Notion-Version: " + api_version_, "Accept: application/json"}, ca_bundle_); }
NotionClient::Response NotionClient::api_patch(const std::string &token, const std::string &path, const Json &body) const { const std::string encoded = body.dump(); return perform_request("https://api.notion.com" + path, {"Authorization: Bearer " + token, "Notion-Version: " + api_version_, "Accept: application/json", "Content-Type: application/json"}, ca_bundle_, &encoded, 8 * 1024 * 1024, "PATCH"); }
NotionClient::Response NotionClient::api_search(const std::string &token, const std::string &query, const std::string &cursor) const {
  Json body = Json::object(); body["page_size"] = Json(100.0); Json filter = Json::object(); filter["property"] = Json(std::string("object")); filter["value"] = Json(std::string("page")); body["filter"] = filter;
  Json sort = Json::object(); sort["direction"] = Json(std::string("descending")); sort["timestamp"] = Json(std::string("last_edited_time")); body["sort"] = sort; if (!query.empty()) body["query"] = Json(query); if (!cursor.empty()) body["start_cursor"] = Json(cursor);
  const std::string encoded = body.dump(); return perform_request("https://api.notion.com/v1/search", {"Authorization: Bearer " + token, "Notion-Version: " + api_version_, "Accept: application/json", "Content-Type: application/json"}, ca_bundle_, &encoded);
}
std::string NotionClient::error_message(const Response &response) { try { const Json body = Json::parse(response.body); if (!body.get("message").string().empty()) return body.get("message").string(); } catch (...) {} return "Notion returned HTTP " + std::to_string(response.status); }
bool NotionClient::validate_token(const std::string &token, std::string &error) const { try { auto response = api_get(token, "/v1/users/me"); if (response.status == 200) return true; error = error_message(response); } catch (const std::exception &e) { error = e.what(); } return false; }
std::vector<PageSummary> NotionClient::search_pages(const std::string &token, const std::string &query, std::string &error) const {
  std::vector<PageSummary> pages;
  try {
    std::string cursor;
    do {
      auto response = api_search(token, query, cursor);
      if (response.status != 200) { error = error_message(response); return pages; }
      const Json root = Json::parse(response.body);
      for (const auto &value : root.get("results").items()) {
        if (value.get("object").string() != "page" || value.get("in_trash").boolean()) continue;
        pages.push_back({value.get("id").string(), title_of(value), {}, value.get("last_edited_time").string()});
      }
      cursor = root.get("has_more").boolean() ? root.get("next_cursor").string() : std::string{};
    } while (!cursor.empty() && pages.size() < 1000);
  } catch (const std::exception &e) { error = e.what(); }
  return pages;
}
bool NotionClient::retrieve_page(const std::string &token, const std::string &page_id, PageDocument &page, std::string &error) const {
  try {
    auto metadata = api_get(token, "/v1/pages/" + page_id);
    if (metadata.status != 200) { error = error_message(metadata); return false; }
    auto markdown = api_get(token, "/v1/pages/" + page_id + "/markdown");
    if (markdown.status != 200) { error = error_message(markdown); return false; }
    Json meta = Json::parse(metadata.body), content = Json::parse(markdown.body);
    std::string source = content.get("markdown").string();
    page = {page_id, title_of(meta), {}, std::move(source),
            content.get("truncated").boolean()};
    return true;
  } catch (const std::exception &e) { error = e.what(); return false; }
}

bool NotionClient::retrieve_page_enrichment(
    const std::string &token, const std::string &page_id,
    std::vector<InlineEquationAnnotation> &annotations,
    std::vector<RichTextColorEnrichment> &colors,
    std::string &error) const {
  try {
    annotations.clear();
    colors.clear();
    std::size_t visited = 0;
    std::size_t editable_index = 0;
    std::function<bool(const std::string &, unsigned)> collect;
    const auto append = [&](const Json &rich_text) {
      for (const auto &part : rich_text.items()) {
        if (part.get("type").string() != "equation") continue;
        const Json &value = part.get("annotations");
        annotations.push_back({
            value.get("bold").boolean(),
            value.get("italic").boolean(),
            value.get("strikethrough").boolean(),
            value.get("underline").boolean(),
            value.get("color").string(),
            part.get("equation").get("expression").string()});
      }
    };
    collect = [&](const std::string &parent, unsigned depth) {
      if (depth > 32 || visited > 4096) {
        error = "This page is too deeply nested to enrich safely";
        return false;
      }
      std::string cursor;
      do {
        std::string path = "/v1/blocks/" + parent + "/children?page_size=100";
        if (!cursor.empty()) path += "&start_cursor=" + cursor;
        const auto response = api_get(token, path);
        if (response.status != 200) {
          error = error_message(response);
          return false;
        }
        const Json root = Json::parse(response.body);
        for (const auto &block : root.get("results").items()) {
          if (++visited > 4096) {
            error = "This page has too many blocks to enrich safely";
            return false;
          }
          const std::string type = block.get("type").string();
          const Json &type_body = block.get(type);
          const Json &rich_text = type_body.get("rich_text");
          append(rich_text);
          if (block_counts_as_editable(block)) {
            RichTextColorEnrichment enrichment;
            enrichment.editable_index = editable_index++;
            enrichment.block_text = logical_rich_text(rich_text);
            std::uint64_t at = 0;
            for (const auto &part : rich_text.items()) {
              const std::uint64_t length =
                  part.get("type").string() == "text" ?
                      utf16_length(part.get("plain_text").string()) : 1;
              const std::string color =
                  part.get("annotations").get("color").string();
              if (!color.empty() && color != "default" && length) {
                if (at > 0xffffffffull ||
                    length > 0xffffffffull - at) {
                  error = "This block is too large to enrich safely";
                  return false;
                }
                if (!enrichment.ranges.empty() &&
                    enrichment.ranges.back().end == at &&
                    enrichment.ranges.back().color == color) {
                  enrichment.ranges.back().end =
                      static_cast<std::uint32_t>(at + length);
                } else {
                  enrichment.ranges.push_back({
                      static_cast<std::uint32_t>(at),
                      static_cast<std::uint32_t>(at + length), color});
                }
              }
              at += length;
            }
            const std::size_t first =
                enrichment.block_text.find_first_not_of(" \t\r");
            const std::size_t last =
                enrichment.block_text.find_last_not_of(" \t\r");
            if (first != std::string::npos &&
                (first != 0 || last + 1 != enrichment.block_text.size())) {
              const std::uint32_t leading = static_cast<std::uint32_t>(
                  utf16_length(enrichment.block_text.substr(0, first)));
              const std::uint32_t visible_end = static_cast<std::uint32_t>(
                  utf16_length(enrichment.block_text.substr(0, last + 1)));
              std::vector<RichTextColorRange> adjusted;
              for (const auto &range : enrichment.ranges) {
                const std::uint32_t start = std::max(range.start, leading);
                const std::uint32_t end = std::min(range.end, visible_end);
                if (start < end)
                  adjusted.push_back(
                      {start - leading, end - leading, range.color});
              }
              enrichment.ranges = std::move(adjusted);
              enrichment.block_text =
                  enrichment.block_text.substr(first, last - first + 1);
            }
            if (!enrichment.ranges.empty())
              colors.push_back(std::move(enrichment));
          }
          for (const auto &cell : type_body.get("cells").items()) append(cell);
          append(type_body.get("caption"));
          if (block.get("has_children").boolean() &&
              type != "child_page" && type != "child_database" &&
              !collect(block.get("id").string(), depth + 1))
            return false;
        }
        cursor = root.get("has_more").boolean() ?
            root.get("next_cursor").string() : std::string{};
      } while (!cursor.empty());
      return true;
    };
    return collect(page_id, 0);
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}
bool NotionClient::build_formatted_rich_text(
    const Json &source, const std::string &expected_text,
    std::uint32_t start_utf16, std::uint32_t end_utf16,
    const std::string &selected_text, const std::string &format,
    Json &formatted, std::string &error, bool *enabled) {
  if (format != "highlight" && format != "bold" &&
      format != "underline" && format != "clear") {
    error = "Unsupported text format"; return false;
  }
  const std::string plain_text = logical_rich_text(source);
  if (plain_text != expected_text) { error = "The page changed; reopen it before formatting text"; return false; }
  if (start_utf16 >= end_utf16 || end_utf16 > utf16_length(plain_text)) { error = "Invalid text selection"; return false; }
  std::size_t selection_start = 0, selection_end = 0;
  if (!utf16_byte(plain_text, start_utf16, selection_start) ||
      !utf16_byte(plain_text, end_utf16, selection_end) ||
      plain_text.substr(selection_start, selection_end - selection_start) != selected_text) {
    error = "The selected text changed; select it again"; return false;
  }
  bool all_enabled = format != "clear", has_overlap = false;
  std::size_t state_at = 0;
  for (const auto &part : source.items()) {
    const std::size_t part_end = state_at +
        (part.get("type").string() == "text" ?
             utf16_length(part.get("plain_text").string()) : 1);
    if (std::max<std::size_t>(state_at, start_utf16) <
        std::min<std::size_t>(part_end, end_utf16)) {
      const std::string type = part.get("type").string();
      const bool equation_format = type == "equation" &&
          (format == "highlight" || format == "underline" ||
           format == "clear");
      if (type != "text" && !equation_format) {
        error = "Selections containing mentions or equations cannot be formatted"; return false;
      }
      has_overlap = true;
      const Json &annotations = part.get("annotations");
      if ((format == "bold" && !annotations.get("bold").boolean()) ||
          (format == "underline" && !annotations.get("underline").boolean()) ||
          (format == "highlight" &&
           annotations.get("color").string() != "yellow_background"))
        all_enabled = false;
    }
    state_at = part_end;
  }
  const bool apply = format != "clear" && has_overlap && !all_enabled;
  if (enabled) *enabled = apply;
  formatted = Json::array();
  std::size_t at = 0;
  for (const auto &part : source.items()) {
    const std::string type = part.get("type").string();
    const std::string plain = part.get("plain_text").string();
    const std::size_t length = type == "text" ? utf16_length(plain) : 1;
    const std::size_t part_end = at + length;
    const std::size_t overlap_start = std::max<std::size_t>(at, start_utf16);
    const std::size_t overlap_end = std::min<std::size_t>(part_end, end_utf16);
    if (overlap_start >= overlap_end) {
      if (type == "text")
        formatted.items().push_back(writable_text(part, part.get("text").get("content").string(), format, false, false));
      else
        formatted.items().push_back(writable_non_text(part));
    }
    else {
      if (type == "equation" &&
          (format == "highlight" || format == "underline" ||
           format == "clear")) {
        formatted.items().push_back(
            writable_formatted_equation(part, format, apply));
      } else if (type != "text") {
        error = "Selections containing mentions or equations cannot be formatted"; return false;
      } else {
        const std::string content = part.get("text").get("content").string();
        if (content != plain) {
          error = "This text cannot be mapped safely; reopen the page and try again"; return false;
        }
        std::size_t before_byte = 0, after_byte = 0;
        if (!utf16_byte(content, overlap_start - at, before_byte) ||
            !utf16_byte(content, overlap_end - at, after_byte)) {
          error = "Invalid text selection"; return false;
        }
        if (before_byte) formatted.items().push_back(writable_text(part, content.substr(0, before_byte), format, false, false));
        formatted.items().push_back(writable_text(part, content.substr(before_byte, after_byte - before_byte), format, true, apply));
        if (after_byte < content.size()) formatted.items().push_back(writable_text(part, content.substr(after_byte), format, false, false));
      }
    }
    at = part_end;
  }
  if (formatted.items().size() > 100) { error = "Formatting would create too many rich-text segments"; return false; }
  return true;
}
bool NotionClient::block_counts_as_editable(const Json &block) {
  const std::string type = block.get("type").string();
  if (type != "paragraph" && type != "bulleted_list_item" &&
      type != "numbered_list_item" && type != "callout") return false;
  if (type != "paragraph" && type != "callout") return true;
  for (const auto &part : block.get(type).get("rich_text").items())
    if (!part.get("plain_text").string().empty()) return true;
  return false;
}
bool NotionClient::format_block_text(const std::string &token,
    const std::string &page_id, std::size_t editable_index,
    const std::string &expected_text, std::uint32_t start_utf16,
    std::uint32_t end_utf16, const std::string &selected_text,
    const std::string &format, bool &enabled, std::string &error) const {
  try {
    EditableBlock target;
    std::size_t seen = 0;
    bool found = false;
    std::function<bool(const std::string &, unsigned)> collect;
    collect = [&](const std::string &parent, unsigned depth) {
      if (depth > 32 || seen > 4096) { error = "This page is too deeply nested to edit safely"; return false; }
      std::string cursor;
      do {
        std::string path = "/v1/blocks/" + parent + "/children?page_size=100";
        if (!cursor.empty()) path += "&start_cursor=" + cursor;
        const auto response = api_get(token, path);
        if (response.status != 200) { error = error_message(response); return false; }
        const Json root = Json::parse(response.body);
        for (const auto &block : root.get("results").items()) {
          const std::string type = block.get("type").string();
          if (block_counts_as_editable(block)) {
            EditableBlock editable{block.get("id").string(), type, {}, block.get(type).get("rich_text")};
            for (const auto &part : editable.rich_text.items()) editable.plain_text += part.get("plain_text").string();
            if (seen == editable_index) { target = std::move(editable); found = true; return true; }
            ++seen;
            if (seen > 4096) { error = "This page has too many editable blocks"; return false; }
          }
          if (block.get("has_children").boolean() &&
              type != "child_page" && type != "child_database" &&
              !collect(block.get("id").string(), depth + 1)) return false;
          if (found) return true;
        }
        cursor = root.get("has_more").boolean() ? root.get("next_cursor").string() : std::string{};
      } while (!cursor.empty() && !found);
      return true;
    };
    if (!collect(page_id, 0)) return false;
    if (!found) { error = "The page changed; reopen it before formatting text"; return false; }
    Json rich_text;
    if (!build_formatted_rich_text(target.rich_text, expected_text, start_utf16,
                                   end_utf16, selected_text, format,
                                   rich_text, error, &enabled)) return false;
    Json body = Json::object(), type_body = Json::object();
    type_body["rich_text"] = rich_text; body[target.type] = type_body;
    const auto response = api_patch(token, "/v1/blocks/" + target.id, body);
    if (response.status != 200) {
      error = error_message(response);
      if (response.status == 403) error += ". Enable update-content capability for this Notion connection";
      return false;
    }
    return true;
  } catch (const std::exception &e) { error = e.what(); return false; }
}
bool NotionClient::retrieve_image(const std::string &url, BinaryResponse &image,
                                  std::string &error,
                                  long *http_status) const {
  if (url.compare(0, 8, "https://") != 0) { error = "Only HTTPS Notion images are allowed"; return false; }
  if (http_status) *http_status = 0;
  try { auto response = perform_request(url, {"Accept: image/*"}, ca_bundle_, nullptr, 16 * 1024 * 1024); if (http_status) *http_status = response.status; if (response.status != 200 || response.content_type.compare(0, 6, "image/") != 0) { error = "Image server returned HTTP " + std::to_string(response.status); return false; } if (response.content_type.compare(0, 13, "image/svg+xml") == 0) add_svg_intrinsic_dimensions(response.body); image = {response.content_type, std::move(response.body)}; return true; } catch (const std::exception &e) { error = e.what(); return false; }
}

bool NotionClient::refresh_image_url(const std::string &token,
                                     const std::string &block_id,
                                     std::string &url,
                                     std::int64_t &expires_at,
                                     std::string &error) const {
  try {
    const auto response = api_get(token, "/v1/blocks/" + block_id);
    if (response.status != 200) { error = error_message(response); return false; }
    const Json block = Json::parse(response.body);
    if (block.get("type").string() != "image" ||
        block.get("image").get("type").string() != "file") {
      error = "The Notion image block no longer contains a hosted file";
      return false;
    }
    url = block.get("image").get("file").get("url").string();
    expires_at = iso8601_timestamp(
        block.get("image").get("file").get("expiry_time").string());
    if (url.compare(0, 8, "https://") != 0) {
      error = "Notion returned an invalid image URL";
      return false;
    }
    return true;
  } catch (const std::exception &exception) {
    error = exception.what();
    return false;
  }
}

bool retrieve_registered_image(ImageRegistry &registry, const std::string &key,
                               std::int64_t now,
                               const ImageDownloadFunction &download,
                               const ImageRefreshFunction &refresh,
                               BinaryResponse &image, std::string &error) {
  const auto request_mutex = registry.request_lock(key);
  std::lock_guard<std::mutex> request_guard(*request_mutex);
  ImageSource source;
  if (!registry.resolve_source(key, source)) {
    error = "Not found";
    return false;
  }
  bool refresh_attempted = false;
  const auto refresh_source = [&]() {
    refresh_attempted = true;
    std::string fresh_url, refresh_error;
    std::int64_t fresh_expiry = 0;
    if (!refresh(source.notion_block_id, fresh_url, fresh_expiry,
                 refresh_error)) {
      error = refresh_error;
      return false;
    }
    registry.update_notion_url(key, fresh_url, fresh_expiry);
    source.url = std::move(fresh_url);
    source.expires_at = fresh_expiry;
    return true;
  };
  if (source.notion_hosted() && source.expires_at > 0 &&
      source.expires_at <= now + 300)
    refresh_source();

  long status = 0;
  if (download(source.url, image, error, &status)) return true;
  if (source.notion_hosted() && !refresh_attempted &&
      (status == 401 || status == 403) && refresh_source()) {
    status = 0;
    return download(source.url, image, error, &status);
  }
  return false;
}
}
