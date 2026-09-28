#include "wire.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static void print_hex(const std::string& label, const std::vector<std::uint8_t>& bytes) {
  std::cerr << label << " (" << bytes.size() << " bytes)\n";
  for (std::size_t offset = 0; offset < bytes.size(); offset += 16) {
    std::cerr << "  " << std::hex << std::setw(4) << std::setfill('0') << offset << "  ";
    for (std::size_t i = 0; i < 16; ++i) {
      if (offset + i < bytes.size()) std::cerr << std::setw(2) << static_cast<unsigned>(bytes[offset + i]) << ' ';
      else std::cerr << "   ";
    }
    std::cerr << " |";
    for (std::size_t i = 0; i < 16 && offset + i < bytes.size(); ++i) {
      auto c = bytes[offset + i];
      std::cerr << (c >= 32 && c <= 126 ? static_cast<char>(c) : '.');
    }
    std::cerr << "|\n";
  }
  std::cerr << std::dec;
}

struct Target { std::string host; std::string port; std::string path; };

static Target parse_target(const std::string& text) {
  auto colon = text.rfind(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 >= text.size()) throw std::runtime_error("bad target");
  auto slash = text.find('/', colon + 1);
  if (slash == std::string::npos || slash == text.size() - 1) throw std::runtime_error("target needs a path");
  return {text.substr(0, colon), text.substr(colon + 1, slash - colon - 1), text.substr(slash)};
}

static conn_t dial(const Target& target) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  if (getaddrinfo(target.host.c_str(), target.port.c_str(), &hints, &results) != 0) throw std::runtime_error("getaddrinfo failed");
  conn_t conn = bad_conn;
  for (auto* item = results; item; item = item->ai_next) {
    conn = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
    if (conn == bad_conn) continue;
    if (::connect(conn, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0) break;
    close_conn(conn);
    conn = bad_conn;
  }
  freeaddrinfo(results);
  if (conn == bad_conn) throw std::runtime_error(conn_error_message("connect failed"));
  return conn;
}

int main(int argc, char** argv) {
  if (argc < 3 || std::string(argv[1]) != "fetch") {
    std::cerr << "usage: cli fetch [-v] host:port/path [more paths...]\n";
    return 2;
  }
  bool verbose = false;
  int first_url = 2;
  if (std::string(argv[first_url]) == "-v") { verbose = true; ++first_url; }
  if (first_url >= argc) return 2;
  try {
    NetGuard guard;
    std::vector<Target> targets;
    for (int i = first_url; i < argc; ++i) targets.push_back(parse_target(argv[i]));
    for (const auto& target : targets) {
      if (target.host != targets.front().host || target.port != targets.front().port) {
        throw std::runtime_error("all paths in one invocation must use the same host and port");
      }
    }
    conn_t conn = dial(targets.front());
    int exit_code = 0;
    for (const auto& target : targets) {
      wire::Ask ask{wire::METHOD_GET, target.path,
          {{"host", target.host + ":" + target.port}, {"user-agent", "cli/1"},
           {"accept", "*/*"}, {"connection", "keep-alive"}}, {}};
      auto payload = wire::encode_ask(ask);
      wire::Packet outgoing{wire::MSG_ASK, 0, static_cast<std::uint8_t>(ask.fields.size()), std::move(payload)};
      if (verbose) print_hex("ask", wire::packet_bytes(outgoing));
      wire::send_packet(conn, outgoing);
      wire::Packet incoming;
      do {
        if (!wire::recv_packet(conn, incoming)) throw std::runtime_error("host closed before reply");
      } while (incoming.type != wire::MSG_REPLY);
      if (verbose) print_hex("reply", wire::packet_bytes(incoming));
      auto reply = wire::decode_reply(incoming);
      std::cout.write(reinterpret_cast<const char*>(reply.body.data()), static_cast<std::streamsize>(reply.body.size()));
      std::cout.flush();
      if (reply.status >= 400) exit_code = 1;
    }
    close_conn(conn);
    return exit_code;
  } catch (const std::exception& error) {
    std::cerr << "cli: " << error.what() << "\n";
    return 1;
  }
}
