#pragma once
#include "potion/app_state.hpp"
#include "potion/markdown.hpp"
#include "potion/notion_client.hpp"
#include "potion/reading_positions.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
namespace potion {
enum class CachePolicy {
  no_store,
  proxied_image,
  immutable_asset
};
const char *cache_control_value(CachePolicy policy) noexcept;
bool is_immutable_asset_path(const std::string &relative_path) noexcept;

struct ServerOptions {
  std::string data_dir{"/var/local/potion"};
  std::string asset_dir{"assets"};
  std::string simulator_asset_dir{"simulator"};
  std::string token_import_path;
  std::string ca_bundle_path;
  std::string start_page_id;
  std::uint16_t port{8766};
  std::uint16_t remote_setup_port{8767};
  bool simulator{};
  std::size_t worker_count{4};
  std::function<bool(const std::string &, std::string &)> token_validator;
};
class HttpServer {
public:
  explicit HttpServer(ServerOptions options);
  ~HttpServer();
  int run();
  void stop() noexcept;
  static void request_stop() noexcept;
  std::uint16_t bound_port() const noexcept { return bound_port_.load(); }
private:
  struct PendingClient {
    int fd{-1};
    bool remote_setup{};
  };
  void handle_client(int client) noexcept;
  void handle_remote_setup_client(int client) noexcept;
  void worker_loop() noexcept;
  void wake_listener() noexcept;
  bool validate_token(const std::string &token, std::string &error);
  std::string remote_setup_url() const;
  std::string remote_setup_path() const;
  void publish_remote_setup(std::string path, std::string url);
  void clear_remote_setup();
  ServerOptions options_;
  AppState state_;
  ReadingPositionStore positions_;
  NotionClient notion_;
  ImageRegistry images_;
  MarkdownRenderer renderer_;
  std::string token_import_message_;
  std::atomic<bool> stopping_{false};
  std::atomic<std::uint16_t> bound_port_{0};
  std::mutex clients_mutex_;
  std::set<int> active_clients_;
  std::mutex pending_mutex_;
  std::condition_variable pending_condition_;
  std::deque<PendingClient> pending_clients_;
  std::vector<std::thread> workers_;
  mutable std::mutex remote_setup_mutex_;
  std::string remote_setup_path_;
  std::string remote_setup_url_;
  int wake_read_{-1}, wake_write_{-1};
};
}
