#pragma once
#include <map>
#include <mutex>
#include <string>
namespace potion {
class ImageRegistry {
public:
  std::string register_url(const std::string &url);
  bool resolve(const std::string &key, std::string &url) const;
  void clear();
private:
  mutable std::mutex mutex_;
  std::map<std::string, std::string> urls_;
  unsigned long next_{1};
};
class MarkdownRenderer {
public:
  explicit MarkdownRenderer(ImageRegistry &images) : images_(images) {}
  std::string render(const std::string &markdown) const;
  static std::string sanitize_url(const std::string &url);
private:
  std::string inline_html(const std::string &text) const;
  ImageRegistry &images_;
};
}

