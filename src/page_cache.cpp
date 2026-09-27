#include "potion/page_cache.hpp"
#include "potion/json.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace potion {
namespace {
bool page_cache_filename(const std::string &name) {
  return (name.compare(0, 6, "pages-") == 0 &&
          name.size() > 11 &&
          name.compare(name.size() - 5, 5, ".json") == 0) ||
         name.compare(0, 7, ".pages-") == 0;
}

bool write_all(int fd, const std::string &data) {
  std::size_t written = 0;
  while (written < data.size()) {
    const ssize_t count = ::write(fd, data.data() + written,
                                  data.size() - written);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    written += static_cast<std::size_t>(count);
  }
  return true;
}
}

SessionPageCache::SessionPageCache(std::string directory,
                                   std::size_t maximum_snapshots,
                                   std::size_t maximum_bytes)
    : directory_(std::move(directory)),
      maximum_snapshots_(maximum_snapshots), maximum_bytes_(maximum_bytes) {}

bool SessionPageCache::clear_locked(std::string *error) noexcept {
  entries_.clear();
  DIR *directory = ::opendir(directory_.c_str());
  if (!directory) {
    if (errno == ENOENT) return true;
    if (error) *error = std::string("Could not open Pages cache: ") +
                        std::strerror(errno);
    return false;
  }
  bool ok = true;
  while (dirent *item = ::readdir(directory)) {
    const std::string name = item->d_name;
    if (!page_cache_filename(name)) continue;
    const std::string path = directory_ + "/" + name;
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode) ||
        ::unlink(path.c_str()) != 0)
      ok = false;
  }
  ::closedir(directory);
  if (!ok && error) *error = "Could not completely clear the Pages cache";
  return ok;
}

bool SessionPageCache::reset(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
  next_file_ = 1;
  use_counter_ = 1;
  if (::mkdir(directory_.c_str(), 0700) != 0 && errno != EEXIST) {
    error = std::string("Could not create Pages cache: ") +
            std::strerror(errno);
    return false;
  }
  struct stat status {};
  if (::lstat(directory_.c_str(), &status) != 0 || !S_ISDIR(status.st_mode)) {
    error = "Pages cache path is not a directory";
    return false;
  }
  if (::chmod(directory_.c_str(), 0700) != 0) {
    error = std::string("Could not protect Pages cache: ") +
            std::strerror(errno);
    return false;
  }
  return clear_locked(&error);
}

void SessionPageCache::clear() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  clear_locked(nullptr);
}

bool SessionPageCache::write_locked(const std::string &query,
                                    const std::vector<PageSummary> &pages,
                                    std::string &path, std::string &error) {
  std::string body = R"({"query":)" + json_escape(query) + R"(,"pages":[)";
  bool first = true;
  for (const auto &page : pages) {
    if (!first) body += ',';
    first = false;
    body += R"({"id":)" + json_escape(page.id) +
            R"(,"title":)" + json_escape(page.title) +
            R"(,"edited":)" + json_escape(page.edited) + "}";
    if (body.size() > maximum_bytes_) {
      error = "The accessible Pages snapshot is too large for this Kindle";
      return false;
    }
  }
  body += "]}";
  if (body.size() > maximum_bytes_) {
    error = "The accessible Pages snapshot is too large for this Kindle";
    return false;
  }
  path = directory_ + "/pages-" + std::to_string(next_file_++) + ".json";
  std::string temporary = directory_ + "/.pages-cache.XXXXXX";
  std::vector<char> temporary_name(temporary.begin(), temporary.end());
  temporary_name.push_back('\0');
  const int fd = ::mkstemp(temporary_name.data());
  if (fd < 0) {
    error = std::string("Could not create Pages cache file: ") +
            std::strerror(errno);
    return false;
  }
  ::fchmod(fd, 0600);
  const bool written = write_all(fd, body);
  const bool closed = ::close(fd) == 0;
  if (!written || !closed ||
      ::rename(temporary_name.data(), path.c_str()) != 0) {
    ::unlink(temporary_name.data());
    error = "Could not save the Pages cache";
    return false;
  }
  return true;
}

void SessionPageCache::enforce_limit_locked() noexcept {
  while (entries_.size() > maximum_snapshots_) {
    auto oldest = std::min_element(
        entries_.begin(), entries_.end(),
        [](const auto &left, const auto &right) {
          return left.second.used < right.second.used;
        });
    if (oldest == entries_.end()) return;
    ::unlink(oldest->second.path.c_str());
    entries_.erase(oldest);
  }
}

bool SessionPageCache::get_or_build(const std::string &query, bool refresh,
                                    const PageSnapshotBuilder &builder,
                                    std::vector<PageSummary> &pages,
                                    std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
  auto existing = entries_.find(query);
  if (existing != entries_.end() && !refresh) {
    existing->second.used = use_counter_++;
    pages = existing->second.pages;
    return true;
  }
  if (maximum_snapshots_ == 0) {
    error = "Pages caching is disabled";
    return false;
  }
  std::vector<PageSummary> built = builder(error);
  if (!error.empty()) return false;
  std::string path;
  if (!write_locked(query, built, path, error)) return false;
  if (existing != entries_.end())
    ::unlink(existing->second.path.c_str());
  entries_[query] = {built, path, use_counter_++};
  enforce_limit_locked();
  pages = std::move(built);
  return true;
}

}
