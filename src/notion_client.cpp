#include "potion/notion_client.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <mutex>
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
                   const std::string &format, bool apply) {
  Json item = Json::object(), text = source.get("text"), annotations = source.get("annotations");
  item["type"] = Json(std::string("text"));
  text["content"] = Json(content);
  item["text"] = text;
  if (apply) {
    if (format == "bold") annotations["bold"] = Json(true);
    else if (format == "underline") annotations["underline"] = Json(true);
    else if (format == "highlight") annotations["color"] = Json(std::string("yellow_background"));
  }
  item["annotations"] = annotations;
  return item;
}
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
  try { auto metadata = api_get(token, "/v1/pages/" + page_id); if (metadata.status != 200) { error = error_message(metadata); return false; } auto markdown = api_get(token, "/v1/pages/" + page_id + "/markdown"); if (markdown.status != 200) { error = error_message(markdown); return false; } Json meta = Json::parse(metadata.body), content = Json::parse(markdown.body); page = {page_id, title_of(meta), {}, content.get("markdown").string(), content.get("truncated").boolean()}; return true; } catch (const std::exception &e) { error = e.what(); return false; }
}
bool NotionClient::build_formatted_rich_text(
    const Json &source, const std::string &expected_text,
    std::uint32_t start_utf16, std::uint32_t end_utf16,
    const std::string &selected_text, const std::string &format,
    Json &formatted, std::string &error) {
  if (format != "highlight" && format != "bold" && format != "underline") {
    error = "Unsupported text format"; return false;
  }
  std::string plain_text;
  for (const auto &part : source.items()) plain_text += part.get("plain_text").string();
  if (plain_text != expected_text) { error = "The page changed; reopen it before formatting text"; return false; }
  if (start_utf16 >= end_utf16 || end_utf16 > utf16_length(plain_text)) { error = "Invalid text selection"; return false; }
  std::size_t selection_start = 0, selection_end = 0;
  if (!utf16_byte(plain_text, start_utf16, selection_start) ||
      !utf16_byte(plain_text, end_utf16, selection_end) ||
      plain_text.substr(selection_start, selection_end - selection_start) != selected_text) {
    error = "The selected text changed; select it again"; return false;
  }
  formatted = Json::array();
  std::size_t at = 0;
  for (const auto &part : source.items()) {
    if (part.get("type").string() != "text") { error = "Selections containing mentions or equations cannot be formatted"; return false; }
    const std::string content = part.get("text").get("content").string();
    const std::size_t length = utf16_length(content), part_end = at + length;
    const std::size_t overlap_start = std::max<std::size_t>(at, start_utf16);
    const std::size_t overlap_end = std::min<std::size_t>(part_end, end_utf16);
    if (overlap_start >= overlap_end) formatted.items().push_back(writable_text(part, content, format, false));
    else {
      std::size_t before_byte = 0, after_byte = 0;
      if (!utf16_byte(content, overlap_start - at, before_byte) ||
          !utf16_byte(content, overlap_end - at, after_byte)) {
        error = "Invalid text selection"; return false;
      }
      if (before_byte) formatted.items().push_back(writable_text(part, content.substr(0, before_byte), format, false));
      formatted.items().push_back(writable_text(part, content.substr(before_byte, after_byte - before_byte), format, true));
      if (after_byte < content.size()) formatted.items().push_back(writable_text(part, content.substr(after_byte), format, false));
    }
    at = part_end;
  }
  if (formatted.items().size() > 100) { error = "Formatting would create too many rich-text segments"; return false; }
  return true;
}
bool NotionClient::format_block_text(const std::string &token,
    const std::string &page_id, std::size_t editable_index,
    const std::string &expected_text, std::uint32_t start_utf16,
    std::uint32_t end_utf16, const std::string &selected_text,
    const std::string &format, std::string &error) const {
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
          if (type == "paragraph" || type == "bulleted_list_item" ||
              type == "numbered_list_item") {
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
                                   rich_text, error)) return false;
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
bool NotionClient::retrieve_image(const std::string &url, BinaryResponse &image, std::string &error) const {
  if (url.compare(0, 8, "https://") != 0) { error = "Only HTTPS Notion images are allowed"; return false; }
  try { auto response = perform_request(url, {"Accept: image/*"}, ca_bundle_, nullptr, 16 * 1024 * 1024); if (response.status != 200 || response.content_type.compare(0, 6, "image/") != 0) { error = "Image server returned HTTP " + std::to_string(response.status); return false; } image = {response.content_type, std::move(response.body)}; return true; } catch (const std::exception &e) { error = e.what(); return false; }
}
}
