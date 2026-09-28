#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using conn_t = SOCKET;
constexpr conn_t bad_conn = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using conn_t = int;
constexpr conn_t bad_conn = -1;
#endif

class NetGuard {
public:
  NetGuard() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) throw std::runtime_error("WSAStartup failed");
#endif
  }
  ~NetGuard() {
#ifdef _WIN32
    WSACleanup();
#endif
  }
};

inline void close_conn(conn_t c) {
  if (c == bad_conn) return;
#ifdef _WIN32
  closesocket(c);
#else
  ::close(c);
#endif
}

inline std::string conn_error_message(const std::string& prefix) {
#ifdef _WIN32
  return prefix + " (" + std::to_string(WSAGetLastError()) + ")";
#else
  return prefix + ": " + std::to_string(errno);
#endif
}
