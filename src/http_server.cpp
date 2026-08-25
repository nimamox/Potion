#include "potion/http_server.hpp"
#include "potion/json.hpp"
#include <arpa/inet.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/input.h>
#endif

namespace potion {
namespace {
volatile std::sig_atomic_t stop_requested = 0;
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
             const std::string &body) {
  std::ostringstream out;
  out << "HTTP/1.1 " << status << ' ' << reason
      << "\r\nContent-Type: " << type << "\r\nContent-Length: " << body.size()
      << "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *"
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
  return "application/octet-stream";
}
bool valid_page_id(const std::string &id) {
  if (id.size() < 32 || id.size() > 36) return false;
  for (char c : id) if (!std::isxdigit(static_cast<unsigned char>(c)) && c != '-') return false;
  return true;
}
#ifdef __linux__
int open_page_keys() {
  for (int i = 0; i < 16; ++i) {
    const std::string event = "event" + std::to_string(i);
    if (trim(read_file("/sys/class/input/" + event + "/device/name")) == "gpiokey")
      return ::open(("/dev/input/" + event).c_str(), O_RDONLY | O_NONBLOCK);
  }
  return -1;
}
#endif
}

HttpServer::HttpServer(ServerOptions options)
    : options_(std::move(options)), state_(options_.data_dir),
      notion_("2026-03-11", options_.ca_bundle_path), renderer_(images_) {
  if (!options_.start_page_id.empty() && !valid_page_id(options_.start_page_id))
    throw std::runtime_error("Invalid startup page id");
  if (!options_.token_import_path.empty()) {
    const auto result = import_token_file(
        options_.token_import_path, state_,
        [this](const std::string &token, std::string &error) {
          return notion_.validate_token(token, error);
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
HttpServer::~HttpServer() { stop(); }
void HttpServer::request_stop() noexcept { stop_requested = 1; }
void HttpServer::wake_listener() noexcept {
  if (wake_write_ >= 0) { const char byte = 1; (void)::write(wake_write_, &byte, 1); }
}
void HttpServer::stop() noexcept {
  if (stopping_.exchange(true)) return;
  input_condition_.notify_all(); pending_condition_.notify_all(); wake_listener();
  std::lock_guard<std::mutex> lock(clients_mutex_);
  for (const int client : active_clients_) ::shutdown(client, SHUT_RDWR);
}
void HttpServer::queue_action(std::string action) {
  { std::lock_guard<std::mutex> lock(input_mutex_); actions_.push_back(std::move(action)); }
  input_condition_.notify_one();
}
void HttpServer::worker_loop() noexcept {
  for (;;) {
    int client = -1;
    {
      std::unique_lock<std::mutex> lock(pending_mutex_);
      pending_condition_.wait(lock, [this] { return stopping_.load() || !pending_clients_.empty(); });
      if (pending_clients_.empty()) return;
      client = pending_clients_.front(); pending_clients_.pop_front();
    }
    if (stopping_.load()) {
      { std::lock_guard<std::mutex> lock(clients_mutex_); active_clients_.erase(client); }
      ::close(client);
    } else handle_client(client);
  }
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
      respond(client, 200, "OK", "application/json", body + "}");
    }
    else if (request.method == "GET" && request.target == "/api/settings")
      respond(client, 200, "OK", "application/json", state_.settings_json());
    else if (request.method == "POST" && request.target == "/api/settings") {
      std::string error;
      if (!state_.set_setting(parameter(request.body, "key"),
                              parameter(request.body, "value"), error))
        throw std::runtime_error(error);
      respond(client, 200, "OK", "application/json", state_.settings_json());
    } else if (request.method == "POST" && request.target == "/api/auth/token") {
      const std::string token = trim(parameter(request.body, "token")); std::string error;
      if (token.empty()) throw std::runtime_error("A Notion access token is required");
      if (!notion_.validate_token(token, error)) throw std::runtime_error(error);
      if (!state_.save_token(token, error)) throw std::runtime_error(error);
      respond(client, 200, "OK", "application/json", R"({"type":"authenticated"})");
    } else if (request.method == "POST" && request.target == "/api/auth/logout") {
      std::string error; if (!state_.clear_token(error)) throw std::runtime_error(error);
      images_.clear();
      respond(client, 200, "OK", "application/json", R"({"type":"logged-out"})");
    } else if (request.method == "GET" && request.target == "/api/pages") {
      if (!state_.authenticated()) throw std::runtime_error("Connect Potion to Notion first");
      std::string error;
      const auto pages = notion_.search_pages(state_.token(), parameter(request.query, "query"), error);
      if (!error.empty()) throw std::runtime_error(error);
      std::string body = R"({"type":"pages","pages":[)"; bool first_page = true;
      for (const auto &page : pages) {
        if (!first_page) body += ',';
        first_page = false;
        body += R"({"id":)" + json_escape(page.id) +
                R"(,"title":)" + json_escape(page.title) +
                R"(,"edited":)" + json_escape(page.edited) + "}";
      }
      body += "]}"; respond(client, 200, "OK", "application/json", body);
    } else if (request.method == "GET" &&
               request.target.compare(0, 11, "/api/pages/") == 0) {
      const std::string id = request.target.substr(11);
      if (!valid_page_id(id)) throw std::runtime_error("Invalid page id");
      PageDocument page; std::string error;
      if (!notion_.retrieve_page(state_.token(), id, page, error))
        throw std::runtime_error(error);
      const std::string body = R"({"type":"page","id":)" + json_escape(page.id) +
        R"(,"title":)" + json_escape(page.title) +
        R"(,"html":)" + json_escape(renderer_.render(page.markdown)) +
        R"(,"truncated":)" + (page.truncated ? "true" : "false") + "}";
      respond(client, 200, "OK", "application/json", body);
    } else if (request.method == "GET" &&
               request.target.compare(0, 12, "/api/images/") == 0) {
      std::string url, error; BinaryResponse image;
      if (!images_.resolve(request.target.substr(12), url))
        respond(client, 404, "Not Found", "text/plain", "Not found\n");
      else if (!notion_.retrieve_image(url, image, error)) throw std::runtime_error(error);
      else respond(client, 200, "OK", image.content_type, image.body);
    } else if (request.method == "GET" && request.target == "/api/input") {
      std::string action;
      {
        std::unique_lock<std::mutex> lock(input_mutex_);
        const auto generation = input_generation_;
        input_condition_.wait_for(lock, options_.input_timeout, [this, generation] {
          return stopping_.load() || input_generation_ != generation || !actions_.empty();
        });
        if (!actions_.empty()) { action = std::move(actions_.front()); actions_.pop_front(); }
      }
      respond(client, 200, "OK", "application/json",
              R"({"type":"input","action":)" + json_escape(action) + "}");
    } else if (request.method == "POST" && request.target == "/api/input/clear") {
      { std::lock_guard<std::mutex> lock(input_mutex_); actions_.clear(); ++input_generation_; }
      input_condition_.notify_all();
      respond(client, 200, "OK", "application/json", R"({"type":"input-cleared"})");
    } else if (request.method == "POST" && request.target == "/api/simulator/input") {
      if (!options_.simulator) throw std::runtime_error("Simulator input is disabled");
      const std::string action = parameter(request.body, "action");
      if (action != "forward" && action != "backward") throw std::runtime_error("Invalid action");
      queue_action(action);
      respond(client, 200, "OK", "application/json", R"({"type":"queued"})");
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
      else respond(client, 200, "OK", mime_type(relative), body);
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
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) throw std::runtime_error("socket failed");
  int pipes[2];
  if (::pipe(pipes) != 0) { ::close(listener); throw std::runtime_error("pipe failed"); }
  wake_read_ = pipes[0]; wake_write_ = pipes[1];
  ::fcntl(wake_read_, F_SETFL, O_NONBLOCK); ::fcntl(wake_write_, F_SETFL, O_NONBLOCK);
  int reuse = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(options_.port);
  if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0 ||
      ::listen(listener, 16) != 0) {
    throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
  }
  socklen_t address_length = sizeof(address);
  getsockname(listener, reinterpret_cast<sockaddr *>(&address), &address_length);
  bound_port_.store(ntohs(address.sin_port));
  for (std::size_t i = 0; i < std::max<std::size_t>(2, options_.worker_count); ++i)
    workers_.emplace_back(&HttpServer::worker_loop, this);
  int keys = -1;
#ifdef __linux__
  keys = open_page_keys();
#endif
  std::cout << "Potion daemon listening at http://127.0.0.1:" << bound_port() << "/\n";
  while (!stopping_.load() && !stop_requested) {
    fd_set set; FD_ZERO(&set); FD_SET(listener, &set); FD_SET(wake_read_, &set);
    int highest = std::max(listener, wake_read_);
    if (keys >= 0) { FD_SET(keys, &set); highest = std::max(highest, keys); }
    timeval timeout{1,0}; const int ready = select(highest + 1, &set, nullptr, nullptr, &timeout);
    if (ready <= 0) continue;
    if (FD_ISSET(wake_read_, &set)) {
      char bytes[32]; while (::read(wake_read_, bytes, sizeof(bytes)) > 0) {}
    }
#ifdef __linux__
    if (keys >= 0 && FD_ISSET(keys, &set)) {
      input_event event{};
      while (::read(keys, &event, sizeof(event)) == sizeof(event))
        if (event.type == EV_KEY && event.value == 1) {
          if (event.code == KEY_PAGEUP) queue_action("forward");
          else if (event.code == KEY_PAGEDOWN) queue_action("backward");
        }
    }
#endif
    if (!FD_ISSET(listener, &set) || stopping_.load()) continue;
    const int client = ::accept(listener, nullptr, nullptr); if (client < 0) continue;
    timeval io{5,0}; setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &io, sizeof(io));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &io, sizeof(io));
    {
      std::lock_guard<std::mutex> clients(clients_mutex_);
      active_clients_.insert(client);
      std::lock_guard<std::mutex> pending(pending_mutex_);
      pending_clients_.push_back(client);
    }
    pending_condition_.notify_one();
  }
  stop(); if (keys >= 0) ::close(keys); ::close(listener);
  for (auto &worker : workers_) if (worker.joinable()) worker.join();
  if (wake_read_ >= 0) ::close(wake_read_);
  if (wake_write_ >= 0) ::close(wake_write_);
  return 0;
}
}
