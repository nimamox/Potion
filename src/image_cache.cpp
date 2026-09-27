#include "potion/image_cache.hpp"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <sys/stat.h>
#include <utime.h>
#include <unistd.h>
#include <vector>

namespace potion {
namespace {
constexpr const char *cache_magic = "POTIONIMG1";

bool cache_filename(const std::string &name) {
  return name.size() == 70 && name.compare(name.size() - 6, 6, ".cache") == 0;
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

SessionImageCache::SessionImageCache(std::string directory,
                                     std::size_t maximum_bytes,
                                     std::size_t maximum_entries)
    : directory_(std::move(directory)), maximum_bytes_(maximum_bytes),
      maximum_entries_(maximum_entries) {}

bool SessionImageCache::valid_key(const std::string &key) noexcept {
  if (key.size() != 64) return false;
  return std::all_of(key.begin(), key.end(), [](unsigned char character) {
    return std::isdigit(character) ||
           (character >= static_cast<unsigned char>('a') &&
            character <= static_cast<unsigned char>('f'));
  });
}

bool SessionImageCache::valid_content_type(
    const std::string &content_type) noexcept {
  return content_type.size() >= 6 && content_type.size() <= 128 &&
         content_type.compare(0, 6, "image/") == 0 &&
         content_type.find_first_of("\r\n") == std::string::npos;
}

std::string SessionImageCache::path_for_key(const std::string &key) const {
  return directory_ + "/" + key + ".cache";
}

bool SessionImageCache::clear_locked(std::string *error) noexcept {
  struct stat directory_status {};
  if (::lstat(directory_.c_str(), &directory_status) != 0) {
    if (errno == ENOENT) return true;
    if (error) *error = std::string("Could not inspect image cache: ") +
                        std::strerror(errno);
    return false;
  }
  if (!S_ISDIR(directory_status.st_mode)) {
    if (error) *error = "Image cache path is not a directory";
    return false;
  }
  DIR *directory = ::opendir(directory_.c_str());
  if (!directory) {
    if (errno == ENOENT) return true;
    if (error) *error = std::string("Could not open image cache: ") +
                        std::strerror(errno);
    return false;
  }
  bool ok = true;
  while (dirent *entry = ::readdir(directory)) {
    const std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    const std::string path = directory_ + "/" + name;
    struct stat status {};
    if (::lstat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode)) {
      ok = false;
      continue;
    }
    if (::unlink(path.c_str()) != 0) ok = false;
  }
  ::closedir(directory);
  if (!ok && error) *error = "Could not completely clear the image cache";
  return ok;
}

bool SessionImageCache::reset(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  error.clear();
  ++generation_;
  if (!clear_locked(&error)) return false;
  if (::mkdir(directory_.c_str(), 0700) != 0 && errno != EEXIST) {
    error = std::string("Could not create image cache: ") +
            std::strerror(errno);
    return false;
  }
  if (::chmod(directory_.c_str(), 0700) != 0) {
    error = std::string("Could not protect image cache: ") +
            std::strerror(errno);
    return false;
  }
  return true;
}

void SessionImageCache::clear() noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  ++generation_;
  clear_locked(nullptr);
}

std::uint64_t SessionImageCache::generation() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return generation_;
}

bool SessionImageCache::load(const std::string &key, std::string &content_type,
                             std::string &body) {
  content_type.clear();
  body.clear();
  if (!valid_key(key)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string path = path_for_key(key);
  struct stat status {};
  if (::stat(path.c_str(), &status) != 0) return false;
  if (!S_ISREG(status.st_mode) || status.st_size < 0 ||
      static_cast<std::size_t>(status.st_size) > maximum_bytes_ + 512) {
    ::unlink(path.c_str());
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  std::string stored_magic, stored_type, size_text;
  if (!input || !std::getline(input, stored_magic) ||
      !std::getline(input, stored_type) || !std::getline(input, size_text) ||
      stored_magic != cache_magic) {
    ::unlink(path.c_str());
    return false;
  }
  std::size_t expected_size = 0, used = 0;
  try {
    expected_size = static_cast<std::size_t>(std::stoull(size_text, &used));
    if (used != size_text.size()) throw std::runtime_error("invalid size");
  } catch (...) {
    ::unlink(path.c_str());
    return false;
  }
  const std::streampos body_at = input.tellg();
  if (!valid_content_type(stored_type) || expected_size > maximum_bytes_ ||
      body_at < 0 || static_cast<std::uint64_t>(body_at) >
                         static_cast<std::uint64_t>(status.st_size) ||
      expected_size != static_cast<std::uint64_t>(status.st_size) -
                           static_cast<std::uint64_t>(body_at)) {
    ::unlink(path.c_str());
    return false;
  }
  body.resize(expected_size);
  if (expected_size != 0) {
    input.read(&body[0], static_cast<std::streamsize>(expected_size));
    if (static_cast<std::size_t>(input.gcount()) != expected_size) {
      body.clear();
      ::unlink(path.c_str());
      return false;
    }
  }
  content_type = stored_type;
  ::utime(path.c_str(), nullptr);
  return true;
}

void SessionImageCache::enforce_limits_locked(
    const std::string &newest_path) noexcept {
  struct Entry { std::string path; off_t size{}; time_t modified{}; };
  std::vector<Entry> entries;
  std::size_t total = 0;
  DIR *directory = ::opendir(directory_.c_str());
  if (!directory) return;
  while (dirent *item = ::readdir(directory)) {
    const std::string name = item->d_name;
    if (!cache_filename(name)) continue;
    const std::string path = directory_ + "/" + name;
    struct stat status {};
    if (::stat(path.c_str(), &status) != 0 || !S_ISREG(status.st_mode))
      continue;
    entries.push_back({path, status.st_size, status.st_mtime});
    if (status.st_size > 0) total += static_cast<std::size_t>(status.st_size);
  }
  ::closedir(directory);
  std::sort(entries.begin(), entries.end(),
            [&newest_path](const Entry &left, const Entry &right) {
    if (left.modified != right.modified) return left.modified < right.modified;
    if (left.path == newest_path) return false;
    if (right.path == newest_path) return true;
    return left.path < right.path;
  });
  std::size_t remaining = entries.size();
  for (const auto &entry : entries) {
    if (total <= maximum_bytes_ && remaining <= maximum_entries_) break;
    if (::unlink(entry.path.c_str()) == 0) {
      if (entry.size > 0) total -= std::min<std::size_t>(
          total, static_cast<std::size_t>(entry.size));
      if (remaining > 0) --remaining;
    }
  }
}

bool SessionImageCache::store(const std::string &key,
                              const std::string &content_type,
                              const std::string &body,
                              std::uint64_t expected_generation) {
  if (!valid_key(key) || !valid_content_type(content_type) ||
      body.size() > maximum_bytes_ || maximum_entries_ == 0)
    return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation_ != expected_generation) return false;
  if (::mkdir(directory_.c_str(), 0700) != 0 && errno != EEXIST) return false;
  struct stat directory_status {};
  if (::lstat(directory_.c_str(), &directory_status) != 0 ||
      !S_ISDIR(directory_status.st_mode))
    return false;
  const std::string header = std::string(cache_magic) + "\n" + content_type +
                             "\n" + std::to_string(body.size()) + "\n";
  std::string temporary = directory_ + "/." + key + ".XXXXXX";
  std::vector<char> temporary_name(temporary.begin(), temporary.end());
  temporary_name.push_back('\0');
  const int fd = ::mkstemp(temporary_name.data());
  if (fd < 0) return false;
  ::fchmod(fd, 0600);
  const bool written = write_all(fd, header) && write_all(fd, body);
  const bool closed = ::close(fd) == 0;
  const std::string destination = path_for_key(key);
  if (!written || !closed ||
      ::rename(temporary_name.data(), destination.c_str()) != 0) {
    ::unlink(temporary_name.data());
    return false;
  }
  enforce_limits_locked(destination);
  return true;
}
}
