#include "potion/reading_positions.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace potion {
namespace {
constexpr char magic[8] = {'P','O','T','N','P','O','S','\0'};
constexpr std::uint16_t format_version = 1;
constexpr std::uint16_t record_size = 26;
constexpr std::size_t header_size = 16;
constexpr std::uint32_t expiry_seconds = 30u * 24u * 60u * 60u;
constexpr std::size_t maximum_entries = 512;
constexpr std::size_t eviction_batch = 100;

int hex_value(char c) noexcept {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
void append_u16(std::string &out, std::uint16_t value) {
  out.push_back(static_cast<char>(value & 0xff));
  out.push_back(static_cast<char>((value >> 8) & 0xff));
}
void append_u32(std::string &out, std::uint32_t value) {
  for (unsigned shift = 0; shift < 32; shift += 8)
    out.push_back(static_cast<char>((value >> shift) & 0xff));
}
std::uint16_t read_u16(const std::string &data, std::size_t at) {
  return static_cast<std::uint16_t>(static_cast<unsigned char>(data[at])) |
         static_cast<std::uint16_t>(static_cast<unsigned char>(data[at + 1]) << 8);
}
std::uint32_t read_u32(const std::string &data, std::size_t at) {
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i)
    value |= static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + i])) << (i * 8);
  return value;
}
}

std::size_t PageUuidHash::operator()(const PageUuid &uuid) const noexcept {
  std::size_t hash = static_cast<std::size_t>(1469598103934665603ull);
  for (const auto byte : uuid.bytes) {
    hash ^= byte;
    hash *= static_cast<std::size_t>(1099511628211ull);
  }
  return hash;
}

bool parse_page_uuid(const std::string &text, PageUuid &uuid) noexcept {
  if (text.size() != 32 && text.size() != 36) return false;
  std::string compact;
  compact.reserve(32);
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '-') {
      if (text.size() != 36 || (i != 8 && i != 13 && i != 18 && i != 23)) return false;
      continue;
    }
    if (hex_value(text[i]) < 0) return false;
    compact.push_back(text[i]);
  }
  if (compact.size() != 32) return false;
  PageUuid parsed;
  for (std::size_t i = 0; i < parsed.bytes.size(); ++i)
    parsed.bytes[i] = static_cast<std::uint8_t>((hex_value(compact[i * 2]) << 4) |
                                                hex_value(compact[i * 2 + 1]));
  uuid = parsed;
  return true;
}

std::string format_page_uuid(const PageUuid &uuid) {
  static const char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(36);
  for (std::size_t i = 0; i < uuid.bytes.size(); ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) result.push_back('-');
    result.push_back(digits[uuid.bytes[i] >> 4]);
    result.push_back(digits[uuid.bytes[i] & 0x0f]);
  }
  return result;
}

std::uint32_t unix_timestamp() noexcept {
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  if (seconds <= 0) return 0;
  if (seconds > 0xffffffffll) return 0xffffffffu;
  return static_cast<std::uint32_t>(seconds);
}

ReadingPositionStore::ReadingPositionStore(std::string directory, std::uint32_t now)
    : directory_(std::move(directory)), path_(directory_ + "/positions.dat") {
  load(now);
}

bool ReadingPositionStore::ensure_directory(std::string &error) const {
  if (::mkdir(directory_.c_str(), 0700) != 0 && errno != EEXIST) {
    error = std::strerror(errno);
    return false;
  }
  ::chmod(directory_.c_str(), 0700);
  return true;
}

void ReadingPositionStore::prune(std::uint32_t now) {
  for (auto it = positions_.begin(); it != positions_.end();) {
    const auto seen = it->second.last_seen;
    if (now >= seen && now - seen > expiry_seconds) it = positions_.erase(it);
    else ++it;
  }
  while (positions_.size() > maximum_entries) {
    std::vector<PageUuid> oldest;
    oldest.reserve(positions_.size());
    for (const auto &entry : positions_) oldest.push_back(entry.first);
    std::sort(oldest.begin(), oldest.end(), [this](const PageUuid &a, const PageUuid &b) {
      return positions_.at(a).last_seen < positions_.at(b).last_seen;
    });
    const auto count = std::min(eviction_batch, oldest.size());
    for (std::size_t i = 0; i < count; ++i) positions_.erase(oldest[i]);
  }
}

void ReadingPositionStore::load(std::uint32_t now) {
  std::ifstream input(path_, std::ios::binary);
  if (!input) return;
  const std::string data((std::istreambuf_iterator<char>(input)),
                         std::istreambuf_iterator<char>());
  if (data.size() < header_size ||
      !std::equal(std::begin(magic), std::end(magic), data.begin()) ||
      read_u16(data, 8) != format_version || read_u16(data, 10) != record_size)
    return;
  const auto count = read_u32(data, 12);
  if (count > 100000 || data.size() != header_size + static_cast<std::size_t>(count) * record_size)
    return;
  std::unordered_map<PageUuid, ReadingPosition, PageUuidHash> loaded;
  std::size_t at = header_size;
  for (std::uint32_t i = 0; i < count; ++i, at += record_size) {
    PageUuid uuid;
    for (std::size_t j = 0; j < uuid.bytes.size(); ++j)
      uuid.bytes[j] = static_cast<std::uint8_t>(data[at + j]);
    loaded[uuid] = {read_u32(data, at + 16), read_u16(data, at + 20),
                    read_u32(data, at + 22)};
  }
  positions_.swap(loaded);
  const auto loaded_size = positions_.size();
  prune(now);
  if (positions_.size() != loaded_size) {
    std::string ignored;
    (void)persist(now, ignored);
  }
}

bool ReadingPositionStore::persist(std::uint32_t now, std::string &error) {
  prune(now);
  if (!ensure_directory(error)) return false;
  std::string data;
  data.reserve(header_size + positions_.size() * record_size);
  data.append(magic, sizeof(magic));
  append_u16(data, format_version);
  append_u16(data, record_size);
  append_u32(data, static_cast<std::uint32_t>(positions_.size()));
  for (const auto &entry : positions_) {
    for (const auto byte : entry.first.bytes) data.push_back(static_cast<char>(byte));
    append_u32(data, entry.second.block_index);
    append_u16(data, entry.second.block_fraction);
    append_u32(data, entry.second.last_seen);
  }
  const std::string temporary = path_ + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) { error = "Cannot write " + temporary; return false; }
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    output.flush();
    if (!output) { error = "Cannot finish " + temporary; ::unlink(temporary.c_str()); return false; }
  }
  ::chmod(temporary.c_str(), 0600);
  if (::rename(temporary.c_str(), path_.c_str()) != 0) {
    error = std::strerror(errno);
    ::unlink(temporary.c_str());
    return false;
  }
  ::chmod(path_.c_str(), 0600);
  return true;
}

std::optional<ReadingPosition> ReadingPositionStore::get(const std::string &page_id) const {
  PageUuid uuid;
  if (!parse_page_uuid(page_id, uuid)) return std::nullopt;
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = positions_.find(uuid);
  return found == positions_.end() ? std::nullopt : std::optional<ReadingPosition>(found->second);
}

std::optional<ReadingPosition> ReadingPositionStore::visit(
    const std::string &page_id, std::uint32_t now, std::string &error) {
  PageUuid uuid;
  if (!parse_page_uuid(page_id, uuid)) { error = "Invalid page id"; return std::nullopt; }
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = positions_.find(uuid);
  const auto previous = found == positions_.end()
      ? std::optional<ReadingPosition>{} : std::optional<ReadingPosition>{found->second};
  if (found == positions_.end()) positions_[uuid] = {0, 0, now};
  else found->second.last_seen = now;
  if (!persist(now, error)) return previous;
  return previous;
}

bool ReadingPositionStore::update(const std::string &page_id,
                                  std::uint32_t block_index,
                                  std::uint16_t block_fraction,
                                  std::uint32_t now, std::string &error) {
  PageUuid uuid;
  if (!parse_page_uuid(page_id, uuid)) { error = "Invalid page id"; return false; }
  std::lock_guard<std::mutex> lock(mutex_);
  positions_[uuid] = {block_index, block_fraction, now};
  return persist(now, error);
}

bool ReadingPositionStore::clear(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  positions_.clear();
  if (::unlink(path_.c_str()) != 0 && errno != ENOENT) {
    error = std::strerror(errno);
    return false;
  }
  ::unlink((path_ + ".tmp").c_str());
  return true;
}

std::size_t ReadingPositionStore::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return positions_.size();
}

}
