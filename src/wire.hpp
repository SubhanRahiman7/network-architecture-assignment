#pragma once

#include "sockets.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace wire {

constexpr std::size_t HEADER_BYTES = 12;
constexpr std::uint8_t MSG_ASK = 1;
constexpr std::uint8_t MSG_REPLY = 2;
constexpr std::uint8_t METHOD_GET = 1;

struct Field {
  std::string name;
  std::string value;
};

struct Packet {
  std::uint8_t type{};
  std::uint8_t flags{};
  std::uint8_t field_count{};
  std::vector<std::uint8_t> payload;
};

struct Ask {
  std::uint8_t method{};
  std::string path;
  std::vector<Field> fields;
  std::vector<std::uint8_t> body;
};

struct Reply {
  std::uint16_t status{};
  std::vector<Field> fields;
  std::vector<std::uint8_t> body;
};

inline const std::array<std::string, 10>& field_table() {
  static const std::array<std::string, 10> names = {
      "host", "user-agent", "accept", "content-length", "content-type",
      "connection", "server", "date", "cache-control", "transfer-encoding"};
  return names;
}

inline void write16(std::vector<std::uint8_t>& out, std::uint16_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

inline void write32(std::vector<std::uint8_t>& out, std::uint32_t value) {
  out.push_back(static_cast<std::uint8_t>(value >> 24));
  out.push_back(static_cast<std::uint8_t>(value >> 16));
  out.push_back(static_cast<std::uint8_t>(value >> 8));
  out.push_back(static_cast<std::uint8_t>(value));
}

inline std::uint16_t read16(const std::vector<std::uint8_t>& in, std::size_t& p) {
  if (p + 2 > in.size()) throw std::runtime_error("short uint16");
  std::uint16_t value = (static_cast<std::uint16_t>(in[p]) << 8) | in[p + 1];
  p += 2;
  return value;
}

inline std::uint32_t read32(const std::vector<std::uint8_t>& in, std::size_t& p) {
  if (p + 4 > in.size()) throw std::runtime_error("short uint32");
  std::uint32_t value = (static_cast<std::uint32_t>(in[p]) << 24) |
                        (static_cast<std::uint32_t>(in[p + 1]) << 16) |
                        (static_cast<std::uint32_t>(in[p + 2]) << 8) | in[p + 3];
  p += 4;
  return value;
}

inline void check_len(std::size_t n, std::size_t limit, const char* what) {
  if (n > limit) throw std::runtime_error(std::string(what) + " too long");
}

inline std::vector<std::uint8_t> encode_fields(const std::vector<Field>& fields) {
  if (fields.size() > 255) throw std::runtime_error("too many fields");
  std::vector<std::uint8_t> out;
  for (const auto& field : fields) {
    std::uint8_t index = 0;
    const auto& names = field_table();
    for (std::size_t i = 0; i < names.size(); ++i) {
      if (field.name == names[i]) index = static_cast<std::uint8_t>(i + 1);
    }
    if (index == 0) {
      check_len(field.name.size(), 255, "field name");
      out.push_back(0);
      out.push_back(static_cast<std::uint8_t>(field.name.size()));
      out.insert(out.end(), field.name.begin(), field.name.end());
    } else {
      out.push_back(index);
    }
    check_len(field.value.size(), 65535, "field value");
    write16(out, static_cast<std::uint16_t>(field.value.size()));
    out.insert(out.end(), field.value.begin(), field.value.end());
  }
  return out;
}

inline std::vector<Field> decode_fields(const std::vector<std::uint8_t>& in,
                                        std::size_t& p, std::uint8_t count) {
  if (count > 32) throw std::runtime_error("too many fields");
  std::vector<Field> fields;
  const auto& names = field_table();
  for (std::uint8_t i = 0; i < count; ++i) {
    if (p >= in.size()) throw std::runtime_error("short field");
    std::uint8_t index = in[p++];
    std::string name;
    if (index == 0) {
      if (p >= in.size()) throw std::runtime_error("short field name");
      std::size_t length = in[p++];
      if (p + length > in.size()) throw std::runtime_error("short field name");
      name.assign(reinterpret_cast<const char*>(&in[p]), length);
      p += length;
    } else {
      if (index > names.size()) throw std::runtime_error("bad field index");
      name = names[index - 1];
    }
    std::uint16_t length = read16(in, p);
    if (p + length > in.size()) throw std::runtime_error("short field value");
    std::string value(reinterpret_cast<const char*>(&in[p]), length);
    p += length;
    fields.push_back({std::move(name), std::move(value)});
  }
  return fields;
}

inline std::vector<std::uint8_t> encode_ask(const Ask& ask) {
  check_len(ask.path.size(), 65535, "path");
  auto fields = encode_fields(ask.fields);
  std::vector<std::uint8_t> out;
  out.push_back(ask.method);
  write16(out, static_cast<std::uint16_t>(ask.path.size()));
  out.insert(out.end(), ask.path.begin(), ask.path.end());
  out.insert(out.end(), fields.begin(), fields.end());
  write32(out, static_cast<std::uint32_t>(ask.body.size()));
  out.insert(out.end(), ask.body.begin(), ask.body.end());
  return out;
}

inline Ask decode_ask(const Packet& packet) {
  if (packet.type != MSG_ASK) throw std::runtime_error("not an ask");
  std::size_t p = 0;
  if (p >= packet.payload.size()) throw std::runtime_error("missing method");
  Ask ask;
  ask.method = packet.payload[p++];
  std::uint16_t path_length = read16(packet.payload, p);
  if (p + path_length > packet.payload.size()) throw std::runtime_error("short path");
  ask.path.assign(reinterpret_cast<const char*>(&packet.payload[p]), path_length);
  p += path_length;
  ask.fields = decode_fields(packet.payload, p, packet.field_count);
  std::uint32_t body_length = read32(packet.payload, p);
  if (p + body_length != packet.payload.size()) throw std::runtime_error("bad body length");
  ask.body.assign(packet.payload.begin() + static_cast<std::ptrdiff_t>(p), packet.payload.end());
  return ask;
}

inline std::vector<std::uint8_t> encode_reply(const Reply& reply) {
  auto fields = encode_fields(reply.fields);
  std::vector<std::uint8_t> out;
  write16(out, reply.status);
  out.insert(out.end(), fields.begin(), fields.end());
  write32(out, static_cast<std::uint32_t>(reply.body.size()));
  out.insert(out.end(), reply.body.begin(), reply.body.end());
  return out;
}

inline Reply decode_reply(const Packet& packet) {
  if (packet.type != MSG_REPLY) throw std::runtime_error("not a reply");
  std::size_t p = 0;
  Reply reply;
  reply.status = read16(packet.payload, p);
  reply.fields = decode_fields(packet.payload, p, packet.field_count);
  std::uint32_t body_length = read32(packet.payload, p);
  if (p + body_length != packet.payload.size()) throw std::runtime_error("bad body length");
  reply.body.assign(packet.payload.begin() + static_cast<std::ptrdiff_t>(p), packet.payload.end());
  return reply;
}

inline bool recv_all(conn_t conn, void* data, std::size_t size) {
  auto* bytes = static_cast<std::uint8_t*>(data);
  std::size_t got = 0;
  while (got < size) {
#ifdef _WIN32
    int n = ::recv(conn, reinterpret_cast<char*>(bytes + got), static_cast<int>(size - got), 0);
#else
    ssize_t n = ::recv(conn, bytes + got, size - got, 0);
#endif
    if (n == 0) return false;
    if (n < 0) throw std::runtime_error(conn_error_message("recv failed"));
    got += static_cast<std::size_t>(n);
  }
  return true;
}

inline void send_all(conn_t conn, const void* data, std::size_t size) {
  auto* bytes = static_cast<const std::uint8_t*>(data);
  std::size_t sent = 0;
  while (sent < size) {
#ifdef _WIN32
    int n = ::send(conn, reinterpret_cast<const char*>(bytes + sent), static_cast<int>(size - sent), 0);
#else
    ssize_t n = ::send(conn, bytes + sent, size - sent, 0);
#endif
    if (n <= 0) throw std::runtime_error(conn_error_message("send failed"));
    sent += static_cast<std::size_t>(n);
  }
}

inline bool recv_packet(conn_t conn, Packet& packet) {
  std::array<std::uint8_t, HEADER_BYTES> header{};
  if (!recv_all(conn, header.data(), header.size())) return false;
  if (header[0] != 'P' || header[1] != 'L' || header[2] != 1 || header[6] != 0 || header[7] != 0) {
    throw std::runtime_error("invalid packet header");
  }
  std::uint32_t payload_length = (static_cast<std::uint32_t>(header[8]) << 24) |
                                 (static_cast<std::uint32_t>(header[9]) << 16) |
                                 (static_cast<std::uint32_t>(header[10]) << 8) | header[11];
  if (payload_length > 64U * 1024U * 1024U) throw std::runtime_error("packet too large");
  packet.type = header[3];
  packet.flags = header[4];
  packet.field_count = header[5];
  packet.payload.resize(payload_length);
  if (payload_length != 0 && !recv_all(conn, packet.payload.data(), payload_length)) {
    throw std::runtime_error("truncated packet payload");
  }
  return true;
}

inline std::vector<std::uint8_t> encode_packet(const Packet& packet) {
  if (packet.payload.size() > UINT32_MAX) throw std::runtime_error("payload too large");
  std::vector<std::uint8_t> bytes;
  bytes.reserve(HEADER_BYTES + packet.payload.size());
  bytes.push_back('P');
  bytes.push_back('L');
  bytes.push_back(1);
  bytes.push_back(packet.type);
  bytes.push_back(packet.flags);
  bytes.push_back(packet.field_count);
  bytes.push_back(0);
  bytes.push_back(0);
  write32(bytes, static_cast<std::uint32_t>(packet.payload.size()));
  bytes.insert(bytes.end(), packet.payload.begin(), packet.payload.end());
  return bytes;
}

inline void send_packet(conn_t conn, const Packet& packet) {
  auto bytes = encode_packet(packet);
  send_all(conn, bytes.data(), bytes.size());
}

inline std::vector<std::uint8_t> packet_bytes(const Packet& packet) { return encode_packet(packet); }

inline std::string field_value(const std::vector<Field>& fields, const std::string& name) {
  for (const auto& field : fields) if (field.name == name) return field.value;
  return {};
}

inline bool has_field(const std::vector<Field>& fields, const std::string& name) {
  for (const auto& field : fields) if (field.name == name && !field.value.empty()) return true;
  return false;
}

} // namespace wire
