#include "potion/http_server.hpp"
#include <csignal>
#include <iostream>
#include <stdexcept>
namespace { void stop_server(int) { potion::HttpServer::request_stop(); } }
int main(int argc, char **argv) {
  try {
    potion::ServerOptions options;
    options.update_checks_enabled = true;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "--help" || arg == "-h") {
        std::cout << "Usage: potiond [--assets DIR] [--data-dir DIR] [--port PORT] "
                     "[--token-import FILE] [--ca-bundle FILE] "
                     "[--start-page PAGE_ID] "
                     "[--simulator --simulator-assets DIR]\n";
        return 0;
      }
      if (arg == "--simulator") { options.simulator = true; continue; }
      if (++i >= argc) throw std::runtime_error("Missing value for " + arg);
      const std::string value = argv[i];
      if (arg == "--assets") options.asset_dir = value;
      else if (arg == "--data-dir") options.data_dir = value;
      else if (arg == "--token-import") options.token_import_path = value;
      else if (arg == "--ca-bundle") options.ca_bundle_path = value;
      else if (arg == "--start-page") options.start_page_id = value;
      else if (arg == "--simulator-assets") options.simulator_asset_dir = value;
      else if (arg == "--port") options.port = static_cast<std::uint16_t>(std::stoul(value));
      else throw std::runtime_error("Unknown option " + arg);
    }
    std::signal(SIGINT, stop_server); std::signal(SIGTERM, stop_server);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
    return potion::HttpServer(std::move(options)).run();
  } catch (const std::exception &error) {
    std::cerr << "potiond: " << error.what() << '\n'; return 1;
  }
}
