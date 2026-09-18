#pragma once
#include "potion/math_renderer.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
namespace potion {
struct ImageSource {
  std::string original_url;
  std::string url;
  std::string notion_block_id;
  std::int64_t expires_at{};
  bool notion_hosted() const noexcept { return !notion_block_id.empty(); }
};
class ImageRegistry {
public:
  std::string register_url(const std::string &url);
  std::string register_notion_url(const std::string &url,
                                  const std::string &block_id,
                                  std::int64_t expires_at);
  bool resolve(const std::string &key, std::string &url) const;
  bool resolve_source(const std::string &key, ImageSource &source) const;
  bool update_notion_url(const std::string &key, const std::string &url,
                         std::int64_t expires_at);
  std::shared_ptr<std::mutex> request_lock(const std::string &key);
  void clear();
private:
  static std::string key_for_url(const std::string &url);
  mutable std::mutex mutex_;
  std::map<std::string, ImageSource> urls_;
  std::map<std::string, std::shared_ptr<std::mutex>> request_locks_;
};
class MarkdownRenderer {
public:
  explicit MarkdownRenderer(ImageRegistry &images) : images_(images) {}
  std::string render(const std::string &markdown) const;
  static std::string sanitize_url(const std::string &url);
private:
  std::string inline_html(const std::string &text) const;
  ImageRegistry &images_;
  MathRenderer math_;
};
}
