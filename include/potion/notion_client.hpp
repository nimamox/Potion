#pragma once
#include "potion/json.hpp"
#include <cstdint>
#include <string>
#include <vector>
namespace potion {
struct PageSummary { std::string id, title, icon, edited; };
struct PageDocument { std::string id, title, icon, markdown; bool truncated{}; };
struct BinaryResponse { std::string content_type, body; };
class NotionClient {
public:
  struct Response { long status{}; std::string content_type, body; };
  explicit NotionClient(std::string api_version = "2026-03-11",
                        std::string ca_bundle = {});
  bool validate_token(const std::string &token, std::string &error) const;
  std::vector<PageSummary> search_pages(const std::string &token,
                                        const std::string &query,
                                        std::string &error) const;
  bool retrieve_page(const std::string &token, const std::string &page_id,
                     PageDocument &page, std::string &error) const;
  bool format_block_text(const std::string &token, const std::string &page_id,
                         std::size_t editable_index,
                         const std::string &expected_text,
                         std::uint32_t start_utf16, std::uint32_t end_utf16,
                         const std::string &selected_text,
                         const std::string &format, bool &enabled,
                         std::string &error) const;
  static bool build_formatted_rich_text(
      const Json &source, const std::string &expected_text,
      std::uint32_t start_utf16, std::uint32_t end_utf16,
      const std::string &selected_text, const std::string &format,
      Json &formatted, std::string &error, bool *enabled = nullptr);
  static bool block_counts_as_editable(const Json &block);
  bool retrieve_image(const std::string &url, BinaryResponse &image,
                      std::string &error) const;
private:
  Response api_get(const std::string &token, const std::string &path) const;
  Response api_patch(const std::string &token, const std::string &path,
                     const Json &body) const;
  Response api_search(const std::string &token, const std::string &query,
                      const std::string &cursor) const;
  static std::string error_message(const Response &response);
  std::string api_version_;
  std::string ca_bundle_;
};
}
