#include "wire.hpp"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using wire::Field;

static std::vector<std::uint8_t> bytes_of(const std::string& text) {
  return {text.begin(), text.end()};
}

static wire::Packet build_reply(std::uint16_t status, std::vector<std::uint8_t> body,
                                const std::string& type = "text/plain") {
  wire::Reply reply{status,
      {{"content-length", std::to_string(body.size())},
       {"content-type", type}, {"connection", "keep-alive"}, {"server", "pl/1"}},
      std::move(body)};
  auto payload = wire::encode_reply(reply);
  return {wire::MSG_REPLY, 0, static_cast<std::uint8_t>(reply.fields.size()), std::move(payload)};
}

static bool parse_num(const std::string& text, std::int64_t& value) {
  if (text.empty()) return false;
  const char* begin = text.data();
  const char* end = begin + text.size();
  auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

static bool parse_query(const std::string& path, std::string& route,
                        std::int64_t& a, std::int64_t& b) {
  auto question = path.find('?');
  route = path.substr(0, question);
  if (question == std::string::npos) return false;
  std::string query = path.substr(question + 1);
  bool got_a = false, got_b = false;
  std::size_t start = 0;
  while (start <= query.size()) {
    auto amp = query.find('&', start);
    std::string item = query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
    auto equal = item.find('=');
    if (equal == std::string::npos) return false;
    std::string name = item.substr(0, equal);
    std::string value = item.substr(equal + 1);
    std::int64_t parsed = 0;
    if (!parse_num(value, parsed)) return false;
    if (name == "a" && !got_a) { a = parsed; got_a = true; }
    else if (name == "b" && !got_b) { b = parsed; got_b = true; }
    else return false;
    if (amp == std::string::npos) break;
    start = amp + 1;
  }
  return got_a && got_b;
}

static wire::Packet handle_ask(const wire::Ask& ask, const fs::path& root) {
  if (ask.method != wire::METHOD_GET) return build_reply(405, {});
  if (!wire::has_field(ask.fields, "host")) return build_reply(400, {});
  if (!ask.body.empty()) return build_reply(400, {});

  std::string route;
  std::int64_t a = 0, b = 0;
  if (ask.path.rfind("/add", 0) == 0 || ask.path.rfind("/sub", 0) == 0 ||
      ask.path.rfind("/mul", 0) == 0 || ask.path.rfind("/div", 0) == 0) {
    if (!parse_query(ask.path, route, a, b)) return build_reply(400, {});
    if (route != "/add" && route != "/sub" && route != "/mul" && route != "/div") {
      return build_reply(404, {});
    }
    std::int64_t result = 0;
    bool overflow = false;
    if (route == "/add") {
      overflow = (b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
                 (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b);
      if (!overflow) result = a + b;
    } else if (route == "/sub") {
      overflow = (b < 0 && a > std::numeric_limits<std::int64_t>::max() + b) ||
                 (b > 0 && a < std::numeric_limits<std::int64_t>::min() + b);
      if (!overflow) result = a - b;
    } else if (route == "/mul") {
      if (a != 0 && b != 0) {
        if (a == -1) overflow = b == std::numeric_limits<std::int64_t>::min();
        else if (b == -1) overflow = a == std::numeric_limits<std::int64_t>::min();
        else if (a > 0) overflow = b > 0 ? a > std::numeric_limits<std::int64_t>::max() / b
                                           : b < std::numeric_limits<std::int64_t>::min() / a;
        else overflow = b > 0 ? a < std::numeric_limits<std::int64_t>::min() / b
                              : a < std::numeric_limits<std::int64_t>::max() / b;
      }
      if (!overflow) result = a * b;
    } else {
      if (b == 0) return build_reply(400, {});
      if (a == std::numeric_limits<std::int64_t>::min() && b == -1) overflow = true;
      else result = a / b;
    }
    if (overflow) return build_reply(400, {});
    return build_reply(200, bytes_of(std::to_string(result)));
  }

  if (ask.path.find("..") != std::string::npos || ask.path.empty() || ask.path[0] != '/') {
    return build_reply(400, {});
  }
  std::string relative = ask.path.substr(1);
  fs::path target_path = fs::weakly_canonical(root / fs::path(relative));
  fs::path root_canon = fs::weakly_canonical(root);
  auto root_text = root_canon.generic_string();
  auto target_text = target_path.generic_string();
  if (target_text != root_text && target_text.rfind(root_text + "/", 0) != 0) {
    return build_reply(400, {});
  }
  std::ifstream input(target_path, std::ios::binary);
  if (!input) return build_reply(404, {});
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return build_reply(200, std::move(data), "application/octet-stream");
}

static conn_t open_listener(const std::string& port) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  addrinfo* results = nullptr;
  if (getaddrinfo(nullptr, port.c_str(), &hints, &results) != 0) throw std::runtime_error("getaddrinfo failed");
  conn_t listener = bad_conn;
  for (auto* item = results; item; item = item->ai_next) {
    listener = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
    if (listener == bad_conn) continue;
    int yes = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));
    if (::bind(listener, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0 && ::listen(listener, 16) == 0) break;
    close_conn(listener);
    listener = bad_conn;
  }
  freeaddrinfo(results);
  if (listener == bad_conn) throw std::runtime_error(conn_error_message("could not bind"));
  return listener;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: host <root> <port>\n";
    return 2;
  }
  try {
    NetGuard guard;
    fs::path root = fs::absolute(argv[1]);
    if (!fs::is_directory(root)) throw std::runtime_error("root is not a directory");
    conn_t listener = open_listener(argv[2]);
    std::cout << "serving " << root << " on port " << argv[2] << "\n";
    while (true) {
      conn_t client = ::accept(listener, nullptr, nullptr);
      if (client == bad_conn) throw std::runtime_error(conn_error_message("accept failed"));
      try {
        while (true) {
          wire::Packet packet;
          if (!wire::recv_packet(client, packet)) break;
          if (packet.type != wire::MSG_ASK) continue;
          try {
            auto ask = wire::decode_ask(packet);
            wire::send_packet(client, handle_ask(ask, root));
          } catch (const std::exception&) {
            wire::send_packet(client, build_reply(400, {}));
          }
        }
      } catch (const std::exception&) {
        // A truncated or malformed peer packet ends this connection.
      }
      close_conn(client);
    }
  } catch (const std::exception& error) {
    std::cerr << "host: " << error.what() << "\n";
    return 1;
  }
}
