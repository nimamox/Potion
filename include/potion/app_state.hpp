#pragma once
#include <functional>
#include <mutex>
#include <set>
#include <string>
namespace potion {
struct Settings {
  double font_scale{1.0};
  std::string card_font{"Bookerly"};
  int code_size{18};
  bool bionic_reading{};
  std::string word_spacing{"normal"};
  std::string line_spacing{"normal"};
  bool night_mode{};
  std::string night_page_mode{"standard"};
  std::string page_button_mode{"normal"};
  std::string page_sort_mode{"opened"};
  std::string rotation_mode{"auto"};
};
class AppState {
public:
  explicit AppState(std::string directory);
  [[nodiscard]] bool authenticated() const;
  [[nodiscard]] std::string token() const;
  [[nodiscard]] Settings settings() const;
  bool save_token(const std::string &token, std::string &error);
  bool clear_token(std::string &error);
  bool set_setting(const std::string &key, const std::string &value, std::string &error);
  [[nodiscard]] std::string settings_json() const;
  [[nodiscard]] bool page_pinned(const std::string &page_id) const;
  bool set_page_pinned(const std::string &page_id, bool pinned,
                       std::string &error);
  bool clear_page_pins(std::string &error);
private:
  bool ensure_directory(std::string &error) const;
  bool persist_settings(std::string &error) const;
  bool persist_page_pins(std::string &error) const;
  void load();
  std::string directory_, token_path_, settings_path_, pins_path_;
  mutable std::mutex mutex_;
  std::string token_;
  Settings settings_;
  std::set<std::string> pinned_pages_;
};

enum class TokenImportResult {
  not_found,
  imported,
  imported_source_remains,
  rejected,
  failed
};

TokenImportResult import_token_file(
    const std::string &path, AppState &state,
    const std::function<bool(const std::string &, std::string &)> &validator,
    std::string &message);
}
