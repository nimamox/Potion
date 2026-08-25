#pragma once
#include "potion/app_state.hpp"
#include "potion/markdown.hpp"
#include "potion/notion_client.hpp"
#include "potion/reading_positions.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
namespace potion {
struct ServerOptions {
  std::string data_dir{"/var/local/potion"};
  std::string asset_dir{"assets"};
  std::string simulator_asset_dir{"simulator"};
  std::string token_import_path;
  std::string ca_bundle_path;
  std::string start_page_id;
  std::uint16_t port{8766};
  bool simulator{};
  std::size_t worker_count{4};
  std::chrono::milliseconds input_timeout{25000};
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
  void handle_client(int client) noexcept;
  void worker_loop() noexcept;
  void queue_action(std::string action);
  void wake_listener() noexcept;
  ServerOptions options_;
  AppState state_;
  ReadingPositionStore positions_;
  NotionClient notion_;
  ImageRegistry images_;
  MarkdownRenderer renderer_;
  std::string token_import_message_;
  std::mutex input_mutex_;
  std::condition_variable input_condition_;
  std::deque<std::string> actions_;
  std::uint64_t input_generation_{};
  std::atomic<bool> stopping_{false};
  std::atomic<std::uint16_t> bound_port_{0};
  std::mutex clients_mutex_;
  std::set<int> active_clients_;
  std::mutex pending_mutex_;
  std::condition_variable pending_condition_;
  std::deque<int> pending_clients_;
  std::vector<std::thread> workers_;
  int wake_read_{-1}, wake_write_{-1};
};
}
