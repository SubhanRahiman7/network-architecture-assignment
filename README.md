# Persistent Link Calculator and File Host

A small C++17 program pair built around a custom binary wire protocol carried over one long-lived TCP connection.

Two executables are built:

- `host` accepts a TCP connection, reads binary packets, runs calculator operations or serves a file from a document root, and keeps the connection open for further packets.
- `cli` opens a single TCP connection, sends one or more requests ("asks") down it, and prints each reply body.

## Supported operations

```text
GET /add?a=2&b=3   -> 200 5
GET /sub?a=10&b=4  -> 200 6
GET /mul?a=6&b=7   -> 200 42
GET /div?a=9&b=3   -> 200 3
```

`host` also serves static files rooted at the directory given on its command line.

## Requirements

- C++17 compiler
- CMake 3.16 or newer
- Winsock on Windows, POSIX sockets elsewhere (handled automatically)

## Build

```sh
cmake -S . -B build
cmake --build build --config Release
```

Produces `host` and `cli`. Single-configuration generators place them in `build/`; multi-config generators (Visual Studio) place them in `build/Release/`.

## Running the host

```sh
./host ./www 9000
```

`www/index.html` is included so the static-file path can be exercised right away.

## Running the client

```sh
./cli fetch "localhost:9000/add?a=2&b=3"
./cli fetch localhost:9000/index.html
./cli fetch "localhost:9000/add?a=2&b=3" "localhost:9000/sub?a=10&b=4" "localhost:9000/mul?a=6&b=7" "localhost:9000/div?a=9&b=3"
./cli fetch -v "localhost:9000/add?a=2&b=3"
```

`-v` prints the full packet bytes for each request/reply as a hexdump on stderr. Bodies go to stdout, and `cli` exits non-zero if any reply status is 400 or above.

## Status codes

- `400` — malformed packet, missing `host` field, bad or duplicate query parameters, division by zero, a non-empty body, or an unsafe path.
- `404` — unknown calculator route, or a file that doesn't exist.
- `405` — any method other than GET.

## Wire format

Every packet starts with a 12-byte big-endian header:

| Bytes | Meaning |
|---|---|
| `0-1` | Magic `PL` |
| `2` | Version `1` |
| `3` | Packet type: ask `1`, reply `2` |
| `4` | Flags, currently `0` |
| `5` | Number of encoded fields |
| `6-7` | Reserved, must be `0` |
| `8-11` | Payload length |

The payload length is read up front, so the receiver always knows exactly where the current packet ends. Field names come from a shared 10-entry table and fall back to an inline length-prefixed name when a field isn't in that table; field values are always length-prefixed. A packet type the receiver doesn't recognize is skipped by its declared payload length instead of closing the connection, so a later protocol version can add packet types safely.

The full wire specification is in [docs/protocol-spec.md](docs/protocol-spec.md) and [docs/protocol-spec.pdf](docs/protocol-spec.pdf).

## What was tested

1. All four calculator operations.
2. Division by zero.
3. Bad values and missing query parameters.
4. An unknown route.
5. A non-GET method.
6. A missing `host` field.
7. A missing file.
8. Several requests answered in order on one connection.
9. An unrecognized packet type followed by a valid ask.
10. `-v` hexdumps of a full ask/reply pair.

The actual byte-level example from a client/server run is documented in [docs/annotated-capture.md](docs/annotated-capture.md).

## Contents

```text
CMakeLists.txt
README.md
src/sockets.hpp
src/wire.hpp
src/host.cpp
src/cli.cpp
www/index.html
docs/protocol-spec.md
docs/protocol-spec.pdf
docs/annotated-capture.md
```
