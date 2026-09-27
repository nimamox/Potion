#pragma once
#include "potion/notion_client.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace potion {

using PageSnapshotBuilder =
    std::function<std::vector<PageSummary>(std::string &)>;

class SessionPageCache {
public:
  explicit SessionPageCache(std::string directory,
                            std::size_t maximum_snapshots = 8,
                            std::size_t maximum_bytes = 2 * 1024 * 1024);
  bool reset(std::string &error);
  void clear() noexcept;
  bool get_or_build(const std::string &query, bool refresh,
                    const PageSnapshotBuilder &builder,
                    std::vector<PageSummary> &pages, std::string &error);

private:
  struct Entry {
    std::vector<PageSummary> pages;
    std::string path;
    std::uint64_t used{};
  };
  bool clear_locked(std::string *error) noexcept;
  bool write_locked(const std::string &query,
                    const std::vector<PageSummary> &pages,
                    std::string &path, std::string &error);
  void enforce_limit_locked() noexcept;

  std::string directory_;
  std::size_t maximum_snapshots_;
  std::size_t maximum_bytes_;
  std::uint64_t next_file_{1};
  std::uint64_t use_counter_{1};
  std::map<std::string, Entry> entries_;
  std::mutex mutex_;
};

}
