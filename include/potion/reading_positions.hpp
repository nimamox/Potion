#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace potion {

struct PageUuid {
  std::array<std::uint8_t, 16> bytes{};
  bool operator==(const PageUuid &other) const noexcept { return bytes == other.bytes; }
};

struct PageUuidHash {
  std::size_t operator()(const PageUuid &uuid) const noexcept;
};

bool parse_page_uuid(const std::string &text, PageUuid &uuid) noexcept;
std::string format_page_uuid(const PageUuid &uuid);
std::uint32_t unix_timestamp() noexcept;

struct ReadingPosition {
  std::uint32_t block_index{};
  std::uint16_t block_fraction{};
  std::uint32_t last_seen{};
};

class ReadingPositionStore {
public:
  explicit ReadingPositionStore(std::string directory,
                                std::uint32_t now = unix_timestamp());
  std::optional<ReadingPosition> get(const std::string &page_id) const;
  std::optional<ReadingPosition> visit(const std::string &page_id,
                                       std::uint32_t now, std::string &error);
  bool update(const std::string &page_id, std::uint32_t block_index,
              std::uint16_t block_fraction, std::uint32_t now,
              std::string &error);
  bool clear(std::string &error);
  std::size_t size() const;

private:
  void load(std::uint32_t now);
  void prune(std::uint32_t now);
  bool persist(std::uint32_t now, std::string &error);
  bool ensure_directory(std::string &error) const;

  std::string directory_, path_;
  mutable std::mutex mutex_;
  std::unordered_map<PageUuid, ReadingPosition, PageUuidHash> positions_;
};

}
