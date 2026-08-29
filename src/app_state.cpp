#include "potion/app_state.hpp"
#include "potion/json.hpp"
#include "potion/reading_positions.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace potion {
namespace {
bool allowed_font(const std::string &font) {
  static const char *fonts[] = {"Amazon Ember", "Baskerville", "Bookerly", "Caecilia", "Caecilia Condensed", "Futura", "Helvetica", "OpenDyslexic", "Palatino"};
  for (const char *candidate : fonts)
    if (font == candidate) return true;
  return false;
}
bool allowed_night_page_mode(const std::string &mode) {
  return mode == "standard" || mode == "palette" || mode == "palette-images";
}
std::string read_file(const std::string &path) { std::ifstream in(path, std::ios::binary); std::ostringstream out; if (in) out << in.rdbuf(); return out.str(); }
bool atomic_write(const std::string &path, const std::string &body, std::string &error) {
  const std::string temporary = path + ".tmp";
  { std::ofstream out(temporary, std::ios::binary | std::ios::trunc); if (!out) { error = "Cannot write " + temporary; return false; } out << body; if (!out) { error = "Cannot finish " + temporary; return false; } }
  ::chmod(temporary.c_str(), 0600);
  if (::rename(temporary.c_str(), path.c_str()) != 0) { error = std::strerror(errno); ::unlink(temporary.c_str()); return false; }
  ::chmod(path.c_str(), 0600); return true;
}
std::string trim(const std::string &value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
}
AppState::AppState(std::string directory) : directory_(std::move(directory)), token_path_(directory_ + "/token"), settings_path_(directory_ + "/state.conf"), pins_path_(directory_ + "/pins.conf") { load(); }
bool AppState::ensure_directory(std::string &error) const { if (::mkdir(directory_.c_str(), 0700) != 0 && errno != EEXIST) { error = std::strerror(errno); return false; } ::chmod(directory_.c_str(), 0700); return true; }
void AppState::load() {
  std::lock_guard<std::mutex> lock(mutex_); token_ = read_file(token_path_);
  while (!token_.empty() && (token_.back() == '\n' || token_.back() == '\r')) token_.pop_back();
  std::ifstream in(settings_path_); std::string line;
  while (std::getline(in, line)) { auto at = line.find('='); if (at == std::string::npos) continue; auto key = line.substr(0, at), value = line.substr(at + 1);
    if (key == "fontScale") { try { double n = std::stod(value); if (n >= .7 && n <= 1.6) settings_.font_scale = n; } catch (...) {} }
    else if (key == "cardFont" && allowed_font(value)) settings_.card_font = value;
    else if (key == "nightMode") settings_.night_mode = value == "1";
    else if (key == "nightPageMode" && allowed_night_page_mode(value)) settings_.night_page_mode = value;
    else if (key == "pageButtonMode" && (value == "normal" || value == "reversed")) settings_.page_button_mode = value;
    else if (key == "pageSortMode" && (value == "opened" || value == "edited")) settings_.page_sort_mode = value;
    else if (key == "rotationMode" && (value == "auto" || value == "locked")) settings_.rotation_mode = value;
  }
  std::ifstream pins(pins_path_);
  while (std::getline(pins, line)) {
    PageUuid uuid;
    if (parse_page_uuid(trim(line), uuid) && pinned_pages_.size() < 512)
      pinned_pages_.insert(format_page_uuid(uuid));
  }
}
bool AppState::authenticated() const { std::lock_guard<std::mutex> lock(mutex_); return !token_.empty(); }
std::string AppState::token() const { std::lock_guard<std::mutex> lock(mutex_); return token_; }
Settings AppState::settings() const { std::lock_guard<std::mutex> lock(mutex_); return settings_; }
bool AppState::save_token(const std::string &token, std::string &error) { std::lock_guard<std::mutex> lock(mutex_); if (!ensure_directory(error) || !atomic_write(token_path_, token + "\n", error)) return false; token_ = token; return true; }
bool AppState::clear_token(std::string &error) { std::lock_guard<std::mutex> lock(mutex_); if (::unlink(token_path_.c_str()) != 0 && errno != ENOENT) { error = std::strerror(errno); return false; } token_.clear(); return true; }
bool AppState::persist_settings(std::string &error) const { if (!ensure_directory(error)) return false; std::ostringstream out; out << "fontScale=" << settings_.font_scale << "\ncardFont=" << settings_.card_font << "\nnightMode=" << (settings_.night_mode ? 1 : 0) << "\nnightPageMode=" << settings_.night_page_mode << "\npageButtonMode=" << settings_.page_button_mode << "\npageSortMode=" << settings_.page_sort_mode << "\nrotationMode=" << settings_.rotation_mode << '\n'; return atomic_write(settings_path_, out.str(), error); }
bool AppState::set_setting(const std::string &key, const std::string &value, std::string &error) { std::lock_guard<std::mutex> lock(mutex_);
  if (key == "fontScale") { try { double n = std::stod(value); if (n < .7 || n > 1.6) throw std::runtime_error("range"); settings_.font_scale = n; } catch (...) { error = "Invalid font scale"; return false; } }
  else if (key == "cardFont") { if (!allowed_font(value)) { error = "Invalid card font"; return false; } settings_.card_font = value; }
  else if (key == "nightMode") { if (value != "0" && value != "1") { error = "Invalid night mode"; return false; } settings_.night_mode = value == "1"; }
  else if (key == "nightPageMode") { if (!allowed_night_page_mode(value)) { error = "Invalid night page mode"; return false; } settings_.night_page_mode = value; }
  else if (key == "pageButtonMode") { if (value != "normal" && value != "reversed") { error = "Invalid page button mode"; return false; } settings_.page_button_mode = value; }
  else if (key == "pageSortMode") { if (value != "opened" && value != "edited") { error = "Invalid page sort mode"; return false; } settings_.page_sort_mode = value; }
  else if (key == "rotationMode") { if (value != "auto" && value != "locked") { error = "Invalid rotation mode"; return false; } settings_.rotation_mode = value; }
  else { error = "Unknown setting"; return false; }
  return persist_settings(error);
}
std::string AppState::settings_json() const { std::lock_guard<std::mutex> lock(mutex_); std::ostringstream out; out << "{\"type\":\"settings\",\"fontScale\":" << settings_.font_scale << ",\"cardFont\":" << json_escape(settings_.card_font) << ",\"nightMode\":" << (settings_.night_mode ? "true" : "false") << ",\"nightPageMode\":" << json_escape(settings_.night_page_mode) << ",\"pageButtonMode\":" << json_escape(settings_.page_button_mode) << ",\"pageSortMode\":" << json_escape(settings_.page_sort_mode) << ",\"rotationMode\":" << json_escape(settings_.rotation_mode) << '}'; return out.str(); }

bool AppState::persist_page_pins(std::string &error) const {
  if (!ensure_directory(error)) return false;
  std::ostringstream out;
  for (const auto &id : pinned_pages_) out << id << '\n';
  return atomic_write(pins_path_, out.str(), error);
}

bool AppState::page_pinned(const std::string &page_id) const {
  PageUuid uuid;
  if (!parse_page_uuid(page_id, uuid)) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  return pinned_pages_.find(format_page_uuid(uuid)) != pinned_pages_.end();
}

bool AppState::set_page_pinned(const std::string &page_id, bool pinned,
                               std::string &error) {
  PageUuid uuid;
  if (!parse_page_uuid(page_id, uuid)) { error = "Invalid page id"; return false; }
  std::lock_guard<std::mutex> lock(mutex_);
  const std::string normalized = format_page_uuid(uuid);
  if (pinned) {
    if (pinned_pages_.find(normalized) == pinned_pages_.end() &&
        pinned_pages_.size() >= 512) {
      error = "Potion can pin at most 512 pages";
      return false;
    }
    pinned_pages_.insert(normalized);
  } else pinned_pages_.erase(normalized);
  return persist_page_pins(error);
}

bool AppState::clear_page_pins(std::string &error) {
  std::lock_guard<std::mutex> lock(mutex_);
  pinned_pages_.clear();
  if (::unlink(pins_path_.c_str()) != 0 && errno != ENOENT) {
    error = std::strerror(errno);
    return false;
  }
  ::unlink((pins_path_ + ".tmp").c_str());
  return true;
}

TokenImportResult import_token_file(
    const std::string &path, AppState &state,
    const std::function<bool(const std::string &, std::string &)> &validator,
    std::string &message) {
  struct stat info{};
  if (::lstat(path.c_str(), &info) != 0) {
    if (errno == ENOENT) return TokenImportResult::not_found;
    message = "Cannot inspect the token import file: " + std::string(std::strerror(errno));
    return TokenImportResult::failed;
  }
  if (!S_ISREG(info.st_mode)) {
    message = "The token import path must be a regular file";
    return TokenImportResult::failed;
  }
  if (info.st_size < 1 || info.st_size > 4096) {
    message = "The token import file is empty or too large";
    return TokenImportResult::rejected;
  }

  std::ifstream input(path, std::ios::binary);
  std::ostringstream contents;
  contents << input.rdbuf();
  if (!input && !input.eof()) {
    message = "Cannot read the token import file";
    return TokenImportResult::failed;
  }
  const std::string token = trim(contents.str());
  if (token.empty() || token.find('\n') != std::string::npos ||
      token.find('\r') != std::string::npos || token.find('\0') != std::string::npos) {
    message = "The token import file must contain only one Notion token";
    return TokenImportResult::rejected;
  }

  std::string error;
  if (!validator(token, error)) {
    message = error.empty() ? "Notion rejected the imported token" : error;
    return TokenImportResult::rejected;
  }
  if (!state.save_token(token, error)) {
    message = "Cannot store the imported token privately: " + error;
    return TokenImportResult::failed;
  }
  if (::unlink(path.c_str()) != 0) {
    message = "The token was imported, but its USB copy could not be removed. "
              "Delete notion-token.txt manually.";
    return TokenImportResult::imported_source_remains;
  }
  return TokenImportResult::imported;
}
}
