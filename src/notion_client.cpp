#include "potion/notion_client.hpp"
#include <curl/curl.h>
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
std::string icon_of(const Json &page) {
  const Json &icon = page.get("icon"); const std::string type = icon.get("type").string();
  if (type == "emoji") return icon.get("emoji").string();
  if (type == "external") return icon.get("external").get("url").string();
  if (type == "file") return icon.get("file").get("url").string();
  return {};
}
}

NotionClient::NotionClient(std::string api_version, std::string ca_bundle)
    : api_version_(std::move(api_version)), ca_bundle_(std::move(ca_bundle)) { std::call_once(curl_once, [] { if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw std::runtime_error("Unable to initialize libcurl"); }); }

static NotionClient::Response perform_request(const std::string &url,
    const std::vector<std::string> &headers, const std::string &ca_bundle,
    const std::string *post = nullptr, std::size_t limit = 8 * 1024 * 1024) {
  CURL *curl = curl_easy_init(); if (!curl) throw std::runtime_error("Unable to initialize HTTPS client");
  NotionClient::Response response; curl_slist *list = nullptr;
  for (const auto &header : headers) list = curl_slist_append(list, header.c_str());
  char error[CURL_ERROR_SIZE]{};
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str()); curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body); curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error); curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L); curl_easy_setopt(curl, CURLOPT_TIMEOUT, 45L);
  if (!ca_bundle.empty()) curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Potion/0.1 Kindle read-only client");
#if LIBCURL_VERSION_NUM >= 0x075500
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https"); curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
  curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS); curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
  if (post) { curl_easy_setopt(curl, CURLOPT_POST, 1L); curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post->c_str()); curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post->size())); }
  const CURLcode result = curl_easy_perform(curl); curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
  char *content_type = nullptr; curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type); if (content_type) response.content_type = content_type;
  curl_slist_free_all(list); curl_easy_cleanup(curl);
  if (result != CURLE_OK) throw std::runtime_error(error[0] ? error : curl_easy_strerror(result));
  if (response.body.size() > limit) throw std::runtime_error("Notion response is too large for this Kindle");
  return response;
}

NotionClient::Response NotionClient::api_get(const std::string &token, const std::string &path) const { return perform_request("https://api.notion.com" + path, {"Authorization: Bearer " + token, "Notion-Version: " + api_version_, "Accept: application/json"}, ca_bundle_); }
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
        pages.push_back({value.get("id").string(), title_of(value), icon_of(value), value.get("last_edited_time").string()});
      }
      cursor = root.get("has_more").boolean() ? root.get("next_cursor").string() : std::string{};
    } while (!cursor.empty() && pages.size() < 1000);
  } catch (const std::exception &e) { error = e.what(); }
  return pages;
}
bool NotionClient::retrieve_page(const std::string &token, const std::string &page_id, PageDocument &page, std::string &error) const {
  try { auto metadata = api_get(token, "/v1/pages/" + page_id); if (metadata.status != 200) { error = error_message(metadata); return false; } auto markdown = api_get(token, "/v1/pages/" + page_id + "/markdown"); if (markdown.status != 200) { error = error_message(markdown); return false; } Json meta = Json::parse(metadata.body), content = Json::parse(markdown.body); page = {page_id, title_of(meta), icon_of(meta), content.get("markdown").string(), content.get("truncated").boolean()}; return true; } catch (const std::exception &e) { error = e.what(); return false; }
}
bool NotionClient::retrieve_image(const std::string &url, BinaryResponse &image, std::string &error) const {
  if (url.compare(0, 8, "https://") != 0) { error = "Only HTTPS Notion images are allowed"; return false; }
  try { auto response = perform_request(url, {"Accept: image/*"}, ca_bundle_, nullptr, 16 * 1024 * 1024); if (response.status != 200 || response.content_type.compare(0, 6, "image/") != 0) { error = "Image server returned HTTP " + std::to_string(response.status); return false; } image = {response.content_type, std::move(response.body)}; return true; } catch (const std::exception &e) { error = e.what(); return false; }
}
}
