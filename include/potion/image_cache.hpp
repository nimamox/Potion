#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace potion {
class SessionImageCache {
public:
  explicit SessionImageCache(std::string directory,
                             std::size_t maximum_bytes = 32 * 1024 * 1024,
                             std::size_t maximum_entries = 128);
  bool reset(std::string &error);
  void clear() noexcept;
  bool load(const std::string &key, std::string &content_type,
            std::string &body);
  std::uint64_t generation() const noexcept;
  bool store(const std::string &key, const std::string &content_type,
             const std::string &body, std::uint64_t expected_generation);

private:
  bool clear_locked(std::string *error) noexcept;
  void enforce_limits_locked(const std::string &newest_path) noexcept;
  std::string path_for_key(const std::string &key) const;
  static bool valid_key(const std::string &key) noexcept;
  static bool valid_content_type(const std::string &content_type) noexcept;

  std::string directory_;
  std::size_t maximum_bytes_;
  std::size_t maximum_entries_;
  mutable std::mutex mutex_;
  std::uint64_t generation_{1};
};
}
