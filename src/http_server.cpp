#include "potion/http_server.hpp"
#include "potion/json.hpp"
#include "potion/orientation.hpp"
#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <ifaddrs.h>
#include <iostream>
#include <net/if.h>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
namespace potion {
namespace {
volatile std::sig_atomic_t stop_requested = 0;
volatile std::sig_atomic_t stop_wake_fd = -1;
struct Request { std::string method, target, query, body; };
std::string trim(const std::string &s) {
  const auto a = s.find_first_not_of(" \t\r\n");
  return a == std::string::npos ? std::string{} :
    s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
std::string decode(const std::string &s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '+') out += ' ';
    else if (s[i] == '%' && i + 2 < s.size() && hex(s[i+1]) >= 0 && hex(s[i+2]) >= 0) {
      out += static_cast<char>((hex(s[i+1]) << 4) | hex(s[i+2])); i += 2;
    } else out += s[i];
  }
  return out;
}
std::string parameter(const std::string &body, const std::string &name) {
  std::size_t at = 0;
  while (at <= body.size()) {
    const auto end = body.find('&', at);
    const auto pair = body.substr(at, end == std::string::npos ? end : end - at);
    const auto equal = pair.find('=');
    if (decode(pair.substr(0, equal)) == name)
      return equal == std::string::npos ? std::string{} : decode(pair.substr(equal + 1));
    if (end == std::string::npos) break;
    at = end + 1;
  }
  return {};
}
bool send_all(int fd, const std::string &data) {
  std::size_t sent = 0;
  while (sent < data.size()) {
#ifdef MSG_NOSIGNAL
    const auto count = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#else
    const auto count = ::send(fd, data.data() + sent, data.size() - sent, 0);
#endif
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    sent += static_cast<std::size_t>(count);
  }
  return true;
}
void respond(int fd, int status, const char *reason, const std::string &type,
             const std::string &body,
             CachePolicy cache_policy = CachePolicy::no_store) {
  std::ostringstream out;
  out << "HTTP/1.1 " << status << ' ' << reason
      << "\r\nContent-Type: " << type << "\r\nContent-Length: " << body.size()
      << "\r\nCache-Control: " << cache_control_value(cache_policy)
      << "\r\nAccess-Control-Allow-Origin: *"
         "\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS"
         "\r\nAccess-Control-Allow-Headers: Content-Type"
         "\r\nConnection: close\r\n\r\n";
  send_all(fd, out.str()); send_all(fd, body);
}
bool read_request(int fd, Request &request) {
  std::string input; char buffer[4096]; std::size_t split = std::string::npos;
  while (input.size() < 131072 && split == std::string::npos) {
    const auto count = ::recv(fd, buffer, sizeof(buffer), 0);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    input.append(buffer, static_cast<std::size_t>(count));
    split = input.find("\r\n\r\n");
  }
  if (split == std::string::npos) return false;
  std::istringstream headers(input.substr(0, split)); std::string line, version;
  if (!std::getline(headers, line)) return false;
  std::istringstream first(line);
  if (!(first >> request.method >> request.target >> version)) return false;
  std::size_t length = 0;
  while (std::getline(headers, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':'); if (colon == std::string::npos) continue;
    std::string name = line.substr(0, colon);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (name == "content-length") length = static_cast<std::size_t>(std::stoul(trim(line.substr(colon + 1))));
  }
  if (length > 131072) return false;
  const auto body_at = split + 4;
  while (input.size() - body_at < length) {
    const auto count = ::recv(fd, buffer, sizeof(buffer), 0);
    if (count <= 0) return false;
    input.append(buffer, static_cast<std::size_t>(count));
  }
  request.body = input.substr(body_at, length);
  const auto query = request.target.find('?');
  if (query != std::string::npos) {
    request.query = request.target.substr(query + 1); request.target.resize(query);
  }
  return true;
}
std::string read_file(const std::string &path) {
  std::ifstream input(path, std::ios::binary); std::ostringstream out;
  if (input) out << input.rdbuf();
  return out.str();
}
std::string mime_type(const std::string &path) {
  if (path.size() >= 5 && path.substr(path.size()-5) == ".html") return "text/html; charset=utf-8";
  if (path.size() >= 4 && path.substr(path.size()-4) == ".css") return "text/css; charset=utf-8";
  if (path.size() >= 3 && path.substr(path.size()-3) == ".js") return "application/javascript; charset=utf-8";
  if (path.size() >= 5 && path.substr(path.size()-5) == ".woff") return "font/woff";
  if (path.size() >= 4 && path.substr(path.size()-4) == ".otf") return "font/otf";
  if (path.size() >= 4 && path.substr(path.size()-4) == ".ttf") return "font/ttf";
  return "application/octet-stream";
}
bool valid_page_id(const std::string &id) {
  PageUuid uuid;
  return parse_page_uuid(id, uuid);
}

std::size_t unsigned_parameter(const std::string &query,
                               const std::string &name,
                               std::size_t fallback,
                               std::size_t maximum) {
  const std::string value = parameter(query, name);
  if (value.empty()) return fallback;
  std::size_t used = 0;
  unsigned long long parsed = 0;
  try {
    parsed = std::stoull(value, &used);
  } catch (...) {
    throw std::runtime_error("Invalid " + name);
  }
  if (used != value.size() || parsed > maximum)
    throw std::runtime_error("Invalid " + name);
  return static_cast<std::size_t>(parsed);
}

std::string html_escape(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (const char c : text) {
    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }
  return out;
}

std::string default_route_interface() {
  std::ifstream routes("/proc/net/route");
  std::string line, interface_name, destination, gateway, flags;
  std::getline(routes, line);
  while (routes >> interface_name >> destination >> gateway >> flags) {
    unsigned long parsed_flags = 0;
    try { parsed_flags = std::stoul(flags, nullptr, 16); } catch (...) { continue; }
    if (destination == "00000000" && (parsed_flags & 1) != 0)
      return interface_name;
    std::getline(routes, line);
  }
  return {};
}

std::string local_ipv4_address() {
  ifaddrs *addresses = nullptr;
  if (::getifaddrs(&addresses) != 0) return {};
  const std::string preferred = default_route_interface();
  std::string fallback;
  for (ifaddrs *entry = addresses; entry; entry = entry->ifa_next) {
    if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET ||
        (entry->ifa_flags & IFF_LOOPBACK) != 0 ||
        (entry->ifa_flags & IFF_UP) == 0)
      continue;
    char address[INET_ADDRSTRLEN]{};
    const auto *ipv4 = reinterpret_cast<sockaddr_in *>(entry->ifa_addr);
    if (!::inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address)))
      continue;
    if (preferred == entry->ifa_name) {
      ::freeifaddrs(addresses);
      return address;
    }
    if (fallback.empty() || fallback.compare(0, 8, "169.254.") == 0)
      fallback = address;
  }
  ::freeifaddrs(addresses);
  return fallback;
}

bool run_command(const std::vector<std::string> &arguments) {
  std::vector<char *> argv;
  argv.reserve(arguments.size() + 1);
  for (const auto &argument : arguments)
    argv.push_back(const_cast<char *>(argument.c_str()));
  argv.push_back(nullptr);

  const pid_t child = ::fork();
  if (child < 0) return false;
  if (child == 0) {
    const int null_fd = ::open("/dev/null", O_WRONLY);
    if (null_fd >= 0) {
      ::dup2(null_fd, STDOUT_FILENO);
      ::dup2(null_fd, STDERR_FILENO);
      ::close(null_fd);
    }
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }
  int status = 0;
  while (::waitpid(child, &status, 0) < 0) {
    if (errno == EINTR) continue;
    return false;
  }
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool set_setup_firewall_rule(std::uint16_t port, bool enabled) {
  const std::string port_text = std::to_string(port);
  const std::string chain = "POTION_SETUP";
  const auto cleanup = [&chain] {
    const bool exists = run_command(
        {"/usr/sbin/iptables", "-L", chain, "-n"});
    while (run_command(
        {"/usr/sbin/iptables", "-D", "INPUT", "-j", chain})) {}
    if (!exists) return true;
    return run_command({"/usr/sbin/iptables", "-F", chain}) &&
           run_command({"/usr/sbin/iptables", "-X", chain});
  };
  if (!cleanup()) return false;
  if (!enabled) return true;
  const bool installed =
      run_command({"/usr/sbin/iptables", "-N", chain}) &&
      run_command({"/usr/sbin/iptables", "-A", chain, "-i", "wlan0", "-p",
                   "tcp", "--dport", port_text, "-j", "ACCEPT"}) &&
      run_command({"/usr/sbin/iptables", "-I", "INPUT", "1", "-j", chain});
  if (!installed) cleanup();
  return installed;
}

int create_listener(std::uint32_t address_value, std::uint16_t port,
                    std::uint16_t &bound_port) {
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) return -1;
  int reuse = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(address_value);
  address.sin_port = htons(port);
  if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 16) != 0) {
    ::close(listener);
    return -1;
  }
  socklen_t length = sizeof(address);
  if (::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) != 0) {
    ::close(listener);
    return -1;
  }
  bound_port = ntohs(address.sin_port);
  return listener;
}

std::string remote_setup_page(const std::string &action,
                              const std::string &error = {}) {
  std::string notice;
  if (!error.empty())
    notice = "<p class=\"error\">" + html_escape(error) + "</p>";
  return "<!doctype html><html><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>Connect Potion</title><style>"
         "body{max-width:34rem;margin:3rem auto;padding:0 1rem;font:18px sans-serif;"
         "color:#111;background:#fff}label{display:block;margin:1.5rem 0 .4rem}"
         "input{box-sizing:border-box;width:100%;padding:.8rem;font:16px monospace}"
         "button{margin-top:1rem;padding:.8rem 1.4rem;border:2px solid #111;"
         "background:#111;color:#fff;font:bold 17px sans-serif}.error{color:#900}"
         "</style></head><body><h1>Connect Potion</h1>"
         "<p>Paste the Notion integration token for this Kindle.</p>" + notice +
         "<form method=\"post\" action=\"" + html_escape(action) + "\">"
         "<label for=\"token\">Notion access token</label>"
         "<input id=\"token\" name=\"token\" type=\"password\" maxlength=\"4096\" "
         "autocomplete=\"off\" autofocus required>"
         "<button type=\"submit\">Connect Potion</button></form></body></html>";
}

std::string remote_setup_success_page() {
  return "<!doctype html><html><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
         "<title>Potion connected</title><style>body{max-width:34rem;margin:3rem auto;"
         "padding:0 1rem;font:18px sans-serif}</style></head><body>"
         "<h1>Potion is connected</h1><p>This temporary setup page is now closed. "
         "Return to the Kindle and choose <strong>Check connection</strong>.</p>"
         "</body></html>";
}
}

const char *cache_control_value(CachePolicy policy) noexcept {
  switch (policy) {
    case CachePolicy::proxied_image:
      return "private, max-age=604800";
    case CachePolicy::immutable_asset:
      return "public, max-age=31536000, immutable";
    case CachePolicy::no_store:
    default:
      return "no-store";
  }
}

bool is_immutable_asset_path(const std::string &relative_path) noexcept {
  if (relative_path.compare(0, 13, "vendor/katex/") == 0) return true;
  if (relative_path.compare(0, 17, "vendor/fast-font/") == 0) return true;
  if (relative_path.compare(0, 18, "vendor/noto-emoji/") == 0) return true;
  return (relative_path.size() >= 5 &&
          relative_path.compare(relative_path.size() - 5, 5, ".woff") == 0) ||
         (relative_path.size() >= 4 &&
          (relative_path.compare(relative_path.size() - 4, 4, ".otf") == 0 ||
           relative_path.compare(relative_path.size() - 4, 4, ".ttf") == 0));
}

HttpServer::HttpServer(ServerOptions options)
    : options_(std::move(options)), state_(options_.data_dir), positions_(options_.data_dir),
      notion_("2026-03-11", options_.ca_bundle_path),
      image_cache_(options_.image_cache_dir, options_.image_cache_max_bytes,
                   options_.image_cache_max_entries),
      page_cache_(options_.page_cache_dir),
      renderer_(images_) {
  if (!options_.start_page_id.empty() && !valid_page_id(options_.start_page_id))
    throw std::runtime_error("Invalid startup page id");
  if (!options_.simulator) {
    std::string rotation_error;
    if (!apply_kindle_rotation(state_.settings().rotation_mode, rotation_error))
      std::cerr << "Rotation restore: " << rotation_error << '\n';
  }
  if (!options_.token_import_path.empty()) {
    const auto result = import_token_file(
        options_.token_import_path, state_,
        [this](const std::string &token, std::string &error) {
          return validate_token(token, error);
        },
        token_import_message_);
    if (result == TokenImportResult::imported)
      std::cerr << "Imported the Notion token and removed its USB copy\n";
    else if (result == TokenImportResult::imported_source_remains ||
             result == TokenImportResult::rejected ||
             result == TokenImportResult::failed)
      std::cerr << "Token import: " << token_import_message_ << '\n';
  }
}
bool HttpServer::validate_token(const std::string &token, std::string &error) {
  if (options_.token_validator)
    return options_.token_validator(token, error);
  return notion_.validate_token(token, error);
}
std::string HttpServer::remote_setup_url() const {
  std::lock_guard<std::mutex> lock(remote_setup_mutex_);
  return remote_setup_url_;
}
std::string HttpServer::remote_setup_path() const {
  std::lock_guard<std::mutex> lock(remote_setup_mutex_);
  return remote_setup_path_;
}
void HttpServer::publish_remote_setup(std::string path, std::string url) {
  std::lock_guard<std::mutex> lock(remote_setup_mutex_);
  remote_setup_path_ = std::move(path);
  remote_setup_url_ = std::move(url);
}
void HttpServer::clear_remote_setup() {
  std::lock_guard<std::mutex> lock(remote_setup_mutex_);
  remote_setup_path_.clear();
  remote_setup_url_.clear();
}
HttpServer::~HttpServer() {
  stop();
  for (auto &worker : workers_)
    if (worker.joinable() && worker.get_id() != std::this_thread::get_id())
      worker.join();
  if (image_prefetch_worker_.joinable() &&
      image_prefetch_worker_.get_id() != std::this_thread::get_id())
    image_prefetch_worker_.join();
  page_cache_.clear();
  image_cache_.clear();
  if (!options_.simulator) {
    std::string ignored;
    apply_kindle_rotation("auto", ignored);
  }
}
void HttpServer::request_stop() noexcept {
  stop_requested = 1;
  const int fd = stop_wake_fd;
  if (fd >= 0) {
    const char byte = 1;
    const auto ignored = ::write(fd, &byte, 1);
    (void)ignored;
  }
}
void HttpServer::wake_listener() noexcept {
  if (wake_write_ >= 0) { const char byte = 1; (void)::write(wake_write_, &byte, 1); }
}
void HttpServer::stop() noexcept {
  if (stopping_.exchange(true)) return;
  pending_condition_.notify_all();
  image_prefetch_condition_.notify_all();
  wake_listener();
  std::lock_guard<std::mutex> lock(clients_mutex_);
  for (const int client : active_clients_) ::shutdown(client, SHUT_RDWR);
}
void HttpServer::worker_loop() noexcept {
  for (;;) {
    int client = -1;
    {
      std::unique_lock<std::mutex> lock(pending_mutex_);
      pending_condition_.wait(lock, [this] { return stopping_.load() || !pending_clients_.empty(); });
      if (pending_clients_.empty()) return;
      const PendingClient pending = pending_clients_.front();
      pending_clients_.pop_front();
      client = pending.fd;
      if (!stopping_.load()) {
        lock.unlock();
        if (pending.remote_setup) handle_remote_setup_client(client);
        else handle_client(client);
        continue;
      }
    }
    if (stopping_.load()) {
      { std::lock_guard<std::mutex> lock(clients_mutex_); active_clients_.erase(client); }
      ::close(client);
    }
  }
}

bool HttpServer::retrieve_image(const std::string &key, BinaryResponse &image,
                                std::string &error) {
  const ImageDownloadFunction download = options_.image_downloader
      ? options_.image_downloader
      : [this](const std::string &url, BinaryResponse &result,
               std::string &download_error, long *status) {
          return notion_.retrieve_image(url, result, download_error, status);
        };
  const ImageRefreshFunction refresh = options_.image_refresher
      ? options_.image_refresher
      : [this](const std::string &block_id, std::string &url,
               std::int64_t &expires_at, std::string &refresh_error) {
          return notion_.refresh_image_url(state_.token(), block_id, url,
                                           expires_at, refresh_error);
        };
  return retrieve_registered_image(
      images_, key, static_cast<std::int64_t>(unix_timestamp()), download,
      refresh, image, error, &image_cache_);
}

void HttpServer::schedule_image_prefetch(
    const std::vector<std::string> &keys) {
  std::lock_guard<std::mutex> lock(image_prefetch_mutex_);
  image_prefetch_queue_.clear();
  image_prefetch_queued_.clear();
  for (const auto &key : keys) {
    if (key == image_prefetch_active_ ||
        image_prefetch_queued_.find(key) != image_prefetch_queued_.end())
      continue;
    if (image_prefetch_queue_.size() >= options_.image_cache_max_entries)
      break;
    image_prefetch_queue_.push_back(key);
    image_prefetch_queued_.insert(key);
  }
  image_prefetch_condition_.notify_one();
}

void HttpServer::clear_image_prefetch() noexcept {
  std::lock_guard<std::mutex> lock(image_prefetch_mutex_);
  image_prefetch_queue_.clear();
  image_prefetch_queued_.clear();
}

void HttpServer::image_prefetch_loop() noexcept {
  for (;;) {
    std::string key;
    {
      std::unique_lock<std::mutex> lock(image_prefetch_mutex_);
      image_prefetch_condition_.wait(lock, [this] {
        return stopping_.load() || !image_prefetch_queue_.empty();
      });
      if (stopping_.load()) return;
      key = image_prefetch_queue_.front();
      image_prefetch_queue_.pop_front();
      image_prefetch_queued_.erase(key);
      image_prefetch_active_ = key;
    }
    BinaryResponse ignored_image;
    std::string ignored_error;
    retrieve_image(key, ignored_image, ignored_error);
    {
      std::lock_guard<std::mutex> lock(image_prefetch_mutex_);
      if (image_prefetch_active_ == key) image_prefetch_active_.clear();
    }
  }
}

void HttpServer::handle_remote_setup_client(int client) noexcept {
  try {
    Request request;
    const std::string path = remote_setup_path();
    if (!read_request(client, request))
      respond(client, 400, "Bad Request", "text/plain; charset=utf-8",
              "Invalid request\n");
    else if (state_.authenticated() || path.empty() || request.target != path)
      respond(client, 404, "Not Found", "text/plain; charset=utf-8",
              "Not found\n");
    else if (request.method == "GET")
      respond(client, 200, "OK", "text/html; charset=utf-8",
              remote_setup_page(path));
    else if (request.method == "POST") {
      const std::string token = trim(parameter(request.body, "token"));
      std::string error;
      if (token.empty()) error = "A Notion access token is required";
      else if (!validate_token(token, error)) {
        if (error.empty()) error = "Notion rejected the token";
      } else if (!state_.save_token(token, error)) {
        if (error.empty()) error = "Potion could not store the token";
      } else {
        respond(client, 200, "OK", "text/html; charset=utf-8",
                remote_setup_success_page());
        wake_listener();
        { std::lock_guard<std::mutex> lock(clients_mutex_);
          active_clients_.erase(client); }
        ::close(client);
        return;
      }
      respond(client, 400, "Bad Request", "text/html; charset=utf-8",
              remote_setup_page(path, error));
    } else
      respond(client, 405, "Method Not Allowed", "text/plain; charset=utf-8",
              "Method not allowed\n");
  } catch (const std::exception &) {
    respond(client, 400, "Bad Request", "text/plain; charset=utf-8",
            "Invalid request\n");
  }
  { std::lock_guard<std::mutex> lock(clients_mutex_); active_clients_.erase(client); }
  ::close(client);
}

void HttpServer::handle_client(int client) noexcept {
  try {
    Request request;
    if (!read_request(client, request))
      respond(client, 400, "Bad Request", "application/json",
              R"({"type":"error","message":"Invalid request"})");
    else if (request.method == "OPTIONS")
      respond(client, 204, "No Content", "text/plain", "");
    else if (request.method == "GET" &&
             (request.target == "/health" || request.target == "/api/status")) {
      std::string body = std::string(R"({"type":"status","version":")") +
                         POTION_VERSION + R"(","authenticated":)" +
                         (state_.authenticated() ? "true" : "false");
      if (!token_import_message_.empty())
        body += R"(,"tokenImportMessage":)" + json_escape(token_import_message_);
      if (!options_.start_page_id.empty())
        body += R"(,"startPageId":)" + json_escape(options_.start_page_id);
      const std::string setup_url = remote_setup_url();
      if (!state_.authenticated() && !setup_url.empty())
        body += R"(,"remoteSetupUrl":)" + json_escape(setup_url);
      respond(client, 200, "OK", "application/json", body + "}");
    }
    else if (request.method == "GET" && request.target == "/api/settings")
      respond(client, 200, "OK", "application/json", state_.settings_json());
    else if (request.method == "POST" && request.target == "/api/settings") {
      const std::string key = parameter(request.body, "key");
      const std::string value = parameter(request.body, "value");
      const std::string previous_rotation = state_.settings().rotation_mode;
      std::string error;
      if (!state_.set_setting(key, value, error))
        throw std::runtime_error(error);
      if (key == "rotationMode" && !options_.simulator &&
          !apply_kindle_rotation(value, error)) {
        std::string rollback_error;
        state_.set_setting("rotationMode", previous_rotation, rollback_error);
        throw std::runtime_error(error);
      }
      respond(client, 200, "OK", "application/json", state_.settings_json());
    } else if (request.method == "POST" && request.target == "/api/auth/token") {
      const std::string token = trim(parameter(request.body, "token")); std::string error;
      if (token.empty()) throw std::runtime_error("A Notion access token is required");
      if (!validate_token(token, error)) throw std::runtime_error(error);
      if (!state_.save_token(token, error)) throw std::runtime_error(error);
      respond(client, 200, "OK", "application/json", R"({"type":"authenticated"})");
      wake_listener();
    } else if (request.method == "POST" && request.target == "/api/auth/logout") {
      std::string error; if (!state_.clear_token(error)) throw std::runtime_error(error);
      if (!positions_.clear(error)) throw std::runtime_error(error);
      if (!state_.clear_page_pins(error)) throw std::runtime_error(error);
      clear_image_prefetch();
      page_cache_.clear();
      image_cache_.clear();
      images_.clear();
      respond(client, 200, "OK", "application/json", R"({"type":"logged-out"})");
      wake_listener();
    } else if (request.method == "GET" && request.target == "/api/pages") {
      if (!state_.authenticated()) throw std::runtime_error("Connect Potion to Notion first");
      const std::string query = trim(parameter(request.query, "query"));
      if (query.size() > 200)
        throw std::runtime_error("Search query is too long");
      const std::string refresh_value = parameter(request.query, "refresh");
      if (!refresh_value.empty() && refresh_value != "1")
        throw std::runtime_error("Invalid refresh value");
      const bool refresh = refresh_value == "1";
      const std::size_t page_size = unsigned_parameter(
          request.query, "pageSize", 20, 50);
      if (page_size == 0) throw std::runtime_error("Invalid pageSize");
      std::size_t offset = unsigned_parameter(
          request.query, "offset", 0, 1000000);
      const std::string page_value = parameter(request.query, "page");
      if (!page_value.empty() && parameter(request.query, "offset").empty()) {
        const std::size_t page = unsigned_parameter(
            request.query, "page", 1, 50000);
        if (page == 0 || page - 1 > 1000000 / page_size)
          throw std::runtime_error("Invalid page");
        offset = (page - 1) * page_size;
      }
      std::string error;
      std::vector<PageSummary> pages;
      const auto build = [this, &query](std::string &build_error) {
        if (options_.page_searcher)
          return options_.page_searcher(
              state_.token(), query, build_error);
        return notion_.search_pages(state_.token(), query, build_error);
      };
      if (!page_cache_.get_or_build(query, refresh, build, pages, error))
        throw std::runtime_error(error);
      const std::string sort_mode = state_.settings().page_sort_mode;
      struct ListedPage {
        PageSummary page;
        std::uint32_t opened{};
        bool pinned{};
      };
      std::vector<ListedPage> listed;
      listed.reserve(pages.size());
      for (const auto &page : pages) {
        const auto position = positions_.get(page.id);
        listed.push_back({page, position ? position->last_seen : 0,
                          state_.page_pinned(page.id)});
      }
      std::sort(listed.begin(), listed.end(),
                [&sort_mode](const ListedPage &left,
                             const ListedPage &right) {
        if (left.pinned != right.pinned) return left.pinned;
        if (sort_mode == "opened" && left.opened != right.opened)
          return left.opened > right.opened;
        if (left.page.edited != right.page.edited)
          return left.page.edited > right.page.edited;
        if (left.page.title != right.page.title)
          return left.page.title < right.page.title;
        return left.page.id < right.page.id;
      });
      const std::size_t total = listed.size();
      if (offset >= total && total != 0)
        offset = ((total - 1) / page_size) * page_size;
      const std::size_t end = std::min(total, offset + page_size);
      std::string body = R"({"type":"pages","pages":[)"; bool first_page = true;
      for (std::size_t i = offset; i < end; ++i) {
        const auto &page = listed[i];
        if (!first_page) body += ',';
        first_page = false;
        body += R"({"id":)" + json_escape(page.page.id) +
                R"(,"title":)" + json_escape(page.page.title) +
                R"(,"edited":)" + json_escape(page.page.edited) +
                R"(,"opened":)" + std::to_string(page.opened) +
                R"(,"pinned":)" + (page.pinned ? "true}" : "false}");
      }
      body += R"(],"sortMode":)" + json_escape(sort_mode) +
              R"(,"offset":)" + std::to_string(offset) +
              R"(,"pageSize":)" + std::to_string(page_size) +
              R"(,"total":)" + std::to_string(total) +
              R"(,"hasMore":)" + (end < total ? "true}" : "false}");
      respond(client, 200, "OK", "application/json", body);
    } else if (request.method == "POST" &&
               request.target.compare(0, 11, "/api/pages/") == 0 &&
               request.target.size() > 15 &&
               request.target.compare(request.target.size() - 4, 4, "/pin") == 0) {
      if (!state_.authenticated()) throw std::runtime_error("Connect Potion to Notion first");
      const std::string id = request.target.substr(11, request.target.size() - 15);
      if (!valid_page_id(id)) throw std::runtime_error("Invalid page id");
      const std::string value = parameter(request.body, "pinned");
      if (value != "0" && value != "1") throw std::runtime_error("Invalid pinned state");
      std::string error;
      if (!state_.set_page_pinned(id, value == "1", error))
        throw std::runtime_error(error);
      respond(client, 200, "OK", "application/json",
              std::string(R"({"type":"pin","pinned":)") +
                  (value == "1" ? "true}" : "false}"));
    } else if (request.method == "POST" &&
               request.target.compare(0, 11, "/api/pages/") == 0 &&
               request.target.size() > 18 &&
               request.target.compare(request.target.size() - 7, 7, "/format") == 0) {
      if (!state_.authenticated()) throw std::runtime_error("Connect Potion to Notion first");
      const std::string id = request.target.substr(11, request.target.size() - 18);
      if (!valid_page_id(id)) throw std::runtime_error("Invalid page id");
      const std::string index_text = parameter(request.body, "editableIndex");
      const std::string start_text = parameter(request.body, "start");
      const std::string end_text = parameter(request.body, "end");
      std::size_t used = 0;
      unsigned long long index = 0, start = 0, end = 0;
      try {
        index = std::stoull(index_text, &used);
        if (used != index_text.size()) throw std::runtime_error("number");
        start = std::stoull(start_text, &used);
        if (used != start_text.size()) throw std::runtime_error("number");
        end = std::stoull(end_text, &used);
        if (used != end_text.size()) throw std::runtime_error("number");
      } catch (...) { throw std::runtime_error("Invalid text selection"); }
      if (index > 4096 || start > 0xffffffffull || end > 0xffffffffull)
        throw std::runtime_error("Invalid text selection");
      std::string error;
      bool enabled = false;
      if (!notion_.format_block_text(
              state_.token(), id, static_cast<std::size_t>(index),
              parameter(request.body, "blockId"),
              parameter(request.body, "blockType"),
              parameter(request.body, "blockText"),
              static_cast<std::uint32_t>(start), static_cast<std::uint32_t>(end),
              parameter(request.body, "selectedText"),
              parameter(request.body, "format"), enabled, error))
        throw std::runtime_error(error);
      respond(client, 200, "OK", "application/json",
              std::string(R"({"type":"formatted","enabled":)") +
                  (enabled ? "true}" : "false}"));
    } else if ((request.method == "GET" || request.method == "POST") &&
               request.target.compare(0, 11, "/api/pages/") == 0 &&
               request.target.size() > 20 &&
               request.target.compare(request.target.size() - 9, 9, "/position") == 0) {
      const std::string id = request.target.substr(11, request.target.size() - 20);
      if (!valid_page_id(id)) throw std::runtime_error("Invalid page id");
      if (request.method == "POST") {
        std::string error;
        const std::string index_text = parameter(request.body, "blockIndex");
        const std::string fraction_text = parameter(request.body, "blockFraction");
        std::size_t used = 0;
        unsigned long long index = 0, fraction = 0;
        try {
          index = std::stoull(index_text, &used);
          if (used != index_text.size()) throw std::runtime_error("number");
          fraction = std::stoull(fraction_text, &used);
          if (used != fraction_text.size()) throw std::runtime_error("number");
        } catch (...) { throw std::runtime_error("Invalid reading position"); }
        index = std::min<unsigned long long>(index, 0xffffffffull);
        fraction = std::min<unsigned long long>(fraction, 65535ull);
        if (!positions_.update(id, static_cast<std::uint32_t>(index),
                               static_cast<std::uint16_t>(fraction),
                               unix_timestamp(), error))
          throw std::runtime_error(error);
      }
      const auto position = positions_.get(id);
      std::string body = R"({"type":"position","position":)";
      if (position)
        body += R"({"blockIndex":)" + std::to_string(position->block_index) +
                R"(,"blockFraction":)" + std::to_string(position->block_fraction) +
                R"(,"lastSeen":)" + std::to_string(position->last_seen) + "}";
      else body += "null";
      respond(client, 200, "OK", "application/json", body + "}");
    } else if (request.method == "GET" &&
               request.target.compare(0, 11, "/api/pages/") == 0 &&
               request.target.size() > 22 &&
               request.target.compare(request.target.size() - 11, 11,
                                      "/enrichment") == 0) {
      if (!state_.authenticated())
        throw std::runtime_error("Connect Potion to Notion first");
      const std::string id = request.target.substr(11, request.target.size() - 22);
      if (!valid_page_id(id)) throw std::runtime_error("Invalid page id");
      std::vector<InlineEquationAnnotation> annotations;
      std::vector<RichTextColorEnrichment> colors;
      std::vector<BlockTargetEnrichment> targets;
      const bool include_targets = parameter(request.query, "targets") == "1";
      std::string error;
      if (!notion_.retrieve_page_enrichment(
              state_.token(), id, annotations, colors, targets,
              include_targets, error))
        throw std::runtime_error(error);
      std::string body = R"({"type":"page-enrichment","equations":[)";
      bool first = true;
      for (const auto &annotation : annotations) {
        if (!first) body += ',';
        first = false;
        body += std::string(R"({"bold":)") +
                (annotation.bold ? "true" : "false") +
                R"(,"italic":)" + (annotation.italic ? "true" : "false") +
                R"(,"strikethrough":)" +
                (annotation.strikethrough ? "true" : "false") +
                R"(,"underline":)" +
                (annotation.underline ? "true" : "false") +
                R"(,"color":)" +
                json_escape(annotation.color.empty() ? "default" :
                                                     annotation.color) +
                R"(,"expression":)" +
                json_escape(annotation.expression) + "}";
      }
      body += R"(],"blocks":[)";
      first = true;
      for (const auto &block : colors) {
        if (!first) body += ',';
        first = false;
        body += R"({"editableIndex":)" +
                std::to_string(block.editable_index) +
                R"(,"blockId":)" + json_escape(block.block_id) +
                R"(,"blockType":)" + json_escape(block.block_type) +
                R"(,"blockText":)" + json_escape(block.block_text) +
                R"(,"colors":[)";
        bool first_range = true;
        for (const auto &range : block.ranges) {
          if (!first_range) body += ',';
          first_range = false;
          body += R"({"start":)" + std::to_string(range.start) +
                  R"(,"end":)" + std::to_string(range.end) +
                  R"(,"color":)" + json_escape(range.color) + "}";
        }
        body += "]}";
      }
      body += R"(],"targets":[)";
      first = true;
      for (const auto &target : targets) {
        if (!first) body += ',';
        first = false;
        body += R"({"blockId":)" + json_escape(target.block_id) +
                R"(,"blockType":)" + json_escape(target.block_type);
        if (!target.expression.empty())
          body += R"(,"expression":)" + json_escape(target.expression);
        body += "}";
      }
      body += "]}";
      respond(client, 200, "OK", "application/json", body);
    } else if (request.method == "GET" &&
               request.target.compare(0, 11, "/api/pages/") == 0) {
      const std::string id = request.target.substr(11);
      if (!valid_page_id(id)) throw std::runtime_error("Invalid page id");
      PageDocument page; std::string error;
      const bool retrieved = options_.page_retriever
          ? options_.page_retriever(state_.token(), id, page, error)
          : notion_.retrieve_page(state_.token(), id, page, error);
      if (!retrieved)
        throw std::runtime_error(error);
      const auto position = positions_.visit(page.id, unix_timestamp(), error);
      if (!error.empty()) std::cerr << "Reading position: " << error << '\n';
      std::vector<std::string> image_keys;
      const std::string html = renderer_.render(page.markdown, &image_keys);
      const bool equation_enrichment =
          html.find("data-potion-expression=") != std::string::npos;
      std::string body = R"({"type":"page","id":)" + json_escape(page.id) +
        R"(,"title":)" + json_escape(page.title) +
        R"(,"html":)" + json_escape(html) +
        R"(,"equationEnrichment":)" +
        (equation_enrichment ? "true" : "false") +
        R"(,"truncated":)" + (page.truncated ? "true" : "false") +
        R"(,"pinned":)" + (state_.page_pinned(page.id) ? "true" : "false") +
        R"(,"position":)";
      if (position)
        body += R"({"blockIndex":)" + std::to_string(position->block_index) +
                R"(,"blockFraction":)" + std::to_string(position->block_fraction) + "}";
      else body += "null";
      body += "}";
      respond(client, 200, "OK", "application/json", body);
      schedule_image_prefetch(image_keys);
    } else if (request.method == "GET" &&
               request.target.compare(0, 12, "/api/images/") == 0) {
      const std::string key = request.target.substr(12);
      std::string error;
      BinaryResponse image;
      if (!retrieve_image(key, image, error)) {
        if (error == "Not found")
          respond(client, 404, "Not Found", "text/plain", "Not found\n");
        else
          throw std::runtime_error(error);
      }
      else respond(client, 200, "OK", image.content_type, image.body,
                   CachePolicy::proxied_image);
    } else if (request.method == "POST" && request.target == "/api/refresh") {
      if (!options_.simulator) {
        const int result = std::system(
          "if [ -x /usr/bin/fbink ]; then /usr/bin/fbink -q -f -s; "
          "elif [ -x /mnt/us/extensions/MRInstaller/bin/PW2/fbink ]; then "
          "/mnt/us/extensions/MRInstaller/bin/PW2/fbink -q -f -s; else exit 1; fi "
          ">/dev/null 2>&1");
        if (result != 0) throw std::runtime_error("FBInk is unavailable");
      }
      respond(client, 200, "OK", "application/json", R"({"type":"refreshed"})");
    } else if (request.method == "POST" && request.target == "/api/quit") {
      respond(client, 200, "OK", "application/json", R"({"type":"quitting"})"); stop();
    } else if (request.method == "GET" && options_.simulator &&
               (request.target == "/simulator" ||
                request.target.compare(0, 11, "/simulator/") == 0)) {
      const std::string relative =
        request.target == "/simulator" || request.target == "/simulator/"
          ? "index.html" : request.target.substr(11);
      if (relative.find("..") != std::string::npos) throw std::runtime_error("Invalid path");
      const std::string body = read_file(options_.simulator_asset_dir + "/" + relative);
      if (body.empty()) respond(client, 404, "Not Found", "text/plain", "Not found\n");
      else respond(client, 200, "OK", mime_type(relative), body);
    } else if (request.method == "GET" &&
               request.target.find("..") == std::string::npos) {
      const std::string relative = request.target == "/" ? "index.html" : request.target.substr(1);
      const std::string body = read_file(options_.asset_dir + "/" + relative);
      if (body.empty()) respond(client, 404, "Not Found", "text/plain", "Not found\n");
      else respond(client, 200, "OK", mime_type(relative), body,
                   is_immutable_asset_path(relative) ?
                       CachePolicy::immutable_asset : CachePolicy::no_store);
    } else respond(client, 404, "Not Found", "application/json",
                   R"({"type":"error","message":"Not found"})");
  } catch (const std::exception &error) {
    respond(client, 400, "Bad Request", "application/json",
            R"({"type":"error","message":)" + json_escape(error.what()) + "}");
  }
  { std::lock_guard<std::mutex> lock(clients_mutex_); active_clients_.erase(client); }
  ::close(client);
}

int HttpServer::run() {
  stop_requested = 0;
  std::string cache_error;
  if (!image_cache_.reset(cache_error))
    std::cerr << "Image cache: " << cache_error << '\n';
  cache_error.clear();
  if (!page_cache_.reset(cache_error))
    std::cerr << "Pages cache: " << cache_error << '\n';
  std::uint16_t main_port = 0;
  const int listener = create_listener(INADDR_LOOPBACK, options_.port, main_port);
  if (listener < 0)
    throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
  int pipes[2];
  if (::pipe(pipes) != 0) { ::close(listener); throw std::runtime_error("pipe failed"); }
  wake_read_ = pipes[0]; wake_write_ = pipes[1];
  stop_wake_fd = wake_write_;
  ::fcntl(wake_read_, F_SETFL, O_NONBLOCK); ::fcntl(wake_write_, F_SETFL, O_NONBLOCK);
  bound_port_.store(main_port);
  image_prefetch_worker_ = std::thread(&HttpServer::image_prefetch_loop, this);
  for (std::size_t i = 0; i < std::max<std::size_t>(2, options_.worker_count); ++i)
    workers_.emplace_back(&HttpServer::worker_loop, this);
  std::cout << "Potion daemon listening at http://127.0.0.1:" << bound_port() << "/\n";
  if (!options_.simulator &&
      !set_setup_firewall_rule(options_.remote_setup_port, false))
    std::cerr << "Could not clean up Potion's previous setup port\n";
  int setup_listener = -1;
  bool setup_firewall_open = false;
  const auto close_setup_listener =
      [this, &setup_listener, &setup_firewall_open] {
        if (setup_listener >= 0) {
          ::close(setup_listener);
          setup_listener = -1;
        }
        if (setup_firewall_open) {
          if (!set_setup_firewall_rule(options_.remote_setup_port, false))
            std::cerr << "Could not close Potion's temporary setup port\n";
          setup_firewall_open = false;
        }
        clear_remote_setup();
      };
  const auto refresh_setup_listener =
      [this, &setup_listener, &setup_firewall_open,
       &close_setup_listener] {
    if (state_.authenticated()) {
      close_setup_listener();
      return;
    }
    if (setup_listener >= 0) return;
    const std::string address = local_ipv4_address();
    if (address.empty()) {
      clear_remote_setup();
      return;
    }
    std::uint16_t port = 0;
    setup_listener = create_listener(INADDR_ANY, options_.remote_setup_port,
                                     port);
    if (setup_listener < 0) {
      clear_remote_setup();
      return;
    }
    if (!options_.simulator && !set_setup_firewall_rule(port, true)) {
      std::cerr << "Could not open Potion's temporary setup port\n";
      ::close(setup_listener);
      setup_listener = -1;
      clear_remote_setup();
      return;
    }
    setup_firewall_open = !options_.simulator;
    const std::string path = "/";
    publish_remote_setup(path, "http://" + address + ":" +
                               std::to_string(port) + "/");
    std::cout << "Temporary Potion setup page available at " <<
                 remote_setup_url() << "\n";
  };
  while (!stopping_.load() && !stop_requested) {
    refresh_setup_listener();
    fd_set set; FD_ZERO(&set); FD_SET(listener, &set); FD_SET(wake_read_, &set);
    int highest = std::max(listener, wake_read_);
    if (setup_listener >= 0) {
      FD_SET(setup_listener, &set);
      highest = std::max(highest, setup_listener);
    }
    const int ready = select(highest + 1, &set, nullptr, nullptr, nullptr);
    if (ready <= 0) continue;
    if (FD_ISSET(wake_read_, &set)) {
      char bytes[32]; while (::read(wake_read_, bytes, sizeof(bytes)) > 0) {}
      refresh_setup_listener();
    }
    if (stopping_.load()) continue;
    const auto accept_client = [this](int source, bool remote_setup) {
      const int client = ::accept(source, nullptr, nullptr);
      if (client < 0) return;
      timeval io{5,0};
      setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &io, sizeof(io));
      setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &io, sizeof(io));
      {
        std::lock_guard<std::mutex> clients(clients_mutex_);
        active_clients_.insert(client);
        std::lock_guard<std::mutex> pending(pending_mutex_);
        pending_clients_.push_back({client, remote_setup});
      }
      pending_condition_.notify_one();
    };
    if (FD_ISSET(listener, &set)) accept_client(listener, false);
    if (setup_listener >= 0 && FD_ISSET(setup_listener, &set))
      accept_client(setup_listener, true);
  }
  stop();
  close_setup_listener();
  ::close(listener);
  for (auto &worker : workers_) if (worker.joinable()) worker.join();
  if (image_prefetch_worker_.joinable()) image_prefetch_worker_.join();
  page_cache_.clear();
  image_cache_.clear();
  stop_wake_fd = -1;
  if (wake_read_ >= 0) ::close(wake_read_);
  if (wake_write_ >= 0) ::close(wake_write_);
  return 0;
}
}
