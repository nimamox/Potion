#pragma once
#include <functional>
#include <mutex>
#include <string>
namespace potion {
struct Settings {
  double font_scale{1.0};
  std::string card_font{"Bookerly"};
  bool night_mode{};
  std::string night_page_mode{"standard"};
  std::string page_button_mode{"normal"};
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
private:
  bool ensure_directory(std::string &error) const;
  bool persist_settings(std::string &error) const;
  void load();
  std::string directory_, token_path_, settings_path_;
  mutable std::mutex mutex_;
  std::string token_;
  Settings settings_;
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
