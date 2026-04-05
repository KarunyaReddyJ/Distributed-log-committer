# TCP Server — Distributed Log Committer Module

A high-performance, non-blocking TCP server module for a distributed log committer, written in C++17. Built on Linux `epoll` (edge-triggered) with a configurable thread pool, so the IO event loop never blocks on your business logic.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Wire Protocol](#wire-protocol)
3. [Project Structure](#project-structure)
4. [Building Locally](#building-locally)
5. [Docker](#docker)
6. [Integrating Into Your Application](#integrating-into-your-application)
   - [Minimal Example](#minimal-example)
   - [Full Log-Committer Integration](#full-log-committer-integration)
   - [Sending Responses Back](#sending-responses-back)
   - [Broadcasting](#broadcasting)
7. [Configuration Reference](#configuration-reference)
8. [Thread Safety Contract](#thread-safety-contract)
9. [Error Handling](#error-handling)
10. [Extending the Server](#extending-the-server)
11. [Troubleshooting](#troubleshooting)

---

## Architecture Overview

```
                        ┌─────────────────────────────────┐
  log-writer clients    │         TcpServer                │
  (your producers)      │                                  │
                        │  ┌──────────┐                   │
  client A ────TCP────► │  │  epoll   │  edge-triggered   │
  client B ────TCP────► │  │  loop    │  (single IO thd)  │
  client C ────TCP────► │  └────┬─────┘                   │
                        │       │ complete frames only     │
                        │  ┌────▼─────────────────────┐   │
                        │  │      ThreadPool (N thds)  │   │
                        │  │  on_message() callbacks   │   │
                        │  └────┬──────────────────────┘   │
                        │       │                          │
                        └───────┼──────────────────────────┘
                                │
                                ▼
                    ┌───────────────────────┐
                    │  Your Log Committer   │
                    │  Core (WAL, Raft,     │
                    │  replication, etc.)   │
                    └───────────────────────┘
```

**Key design decisions:**

| Concern | Choice | Reason |
|---|---|---|
| IO multiplexing | `epoll` edge-triggered | Avoids level-triggered spurious wakeups; single-threaded IO loop stays simple |
| Callback dispatch | Thread pool | Slow commit handlers don't stall the accept loop |
| Framing | 4-byte big-endian length prefix | Simple, language-agnostic; no delimiter scanning |
| Write backpressure | `EPOLLOUT` armed only when write queue is non-empty | Prevents busy-loops on idle connections |
| Shutdown | Pipe-based wakeup of `epoll_wait` | Instant, signal-safe, no timeout polling |

---

## Wire Protocol

Every message on the wire is a **length-prefixed frame**:

```
 0               1               2               3
 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7 0 1 2 3 4 5 6 7
├───────────────────────────────────────────────────────────────────┤
│                  Payload Length  (uint32, big-endian)             │
├───────────────────────────────────────────────────────────────────┤
│                  Payload  (Length bytes)                          │
│                  ...                                              │
└───────────────────────────────────────────────────────────────────┘
```

- **Max payload**: 64 MiB (hard-coded guard in `connection.cpp`; adjust as needed)
- **Encoding**: payload bytes are opaque — use JSON, protobuf, FlatBuffers, or raw bytes

### Python client example

```python
import socket, struct

def send_msg(sock, payload: bytes):
    sock.sendall(struct.pack(">I", len(payload)) + payload)

def recv_msg(sock) -> bytes:
    hdr = sock.recv(4, socket.MSG_WAITALL)
    length = struct.unpack(">I", hdr)[0]
    return sock.recv(length, socket.MSG_WAITALL)

with socket.create_connection(("localhost", 9090)) as s:
    send_msg(s, b"log_entry: txn_id=42 data=hello")
    ack = recv_msg(s)
    print("ACK:", ack.decode())
```

### Go client example

```go
package main

import (
    "encoding/binary"
    "net"
)

func sendMsg(conn net.Conn, payload []byte) error {
    hdr := make([]byte, 4)
    binary.BigEndian.PutUint32(hdr, uint32(len(payload)))
    _, err := conn.Write(append(hdr, payload...))
    return err
}

func recvMsg(conn net.Conn) ([]byte, error) {
    hdr := make([]byte, 4)
    if _, err := io.ReadFull(conn, hdr); err != nil { return nil, err }
    buf := make([]byte, binary.BigEndian.Uint32(hdr))
    _, err := io.ReadFull(conn, buf)
    return buf, err
}
```

---

## Project Structure

```
tcp_server/
├── include/
│   ├── tcp_server.h      # Public API — only file your app needs to #include
│   ├── connection.h      # Per-client read/write state machine
│   └── thread_pool.h     # Work-stealing thread pool
├── src/
│   ├── tcp_server.cpp    # epoll event loop, accept, send, broadcast
│   ├── connection.cpp    # Framed read/write, write queue
│   ├── thread_pool.cpp   # Worker thread management
│   └── main.cpp          # Integration example (replace with your own)
├── CMakeLists.txt
├── Dockerfile
├── docker-compose.yml
└── README.md
```

---

## Building Locally

### Prerequisites

- Linux kernel ≥ 3.9 (epoll edge-triggered + `EPOLLRDHUP` support)
- GCC ≥ 9 or Clang ≥ 10 (C++17 required)
- CMake ≥ 3.16

### Build steps

```bash
# 1. Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Compile
cmake --build build --parallel $(nproc)

# 3. Run the demo server
./build/log_committer_tcp
```

The demo server listens on `0.0.0.0:9090`, echoes `ACK` for every message, and shuts down cleanly on `SIGINT` / `SIGTERM`.

### Linking as a library in your CMake project

```cmake
# In your top-level CMakeLists.txt
add_subdirectory(tcp_server)           # path to this directory

target_link_libraries(your_target PRIVATE tcp_server_lib)
```

---

## Docker

### Build the image

```bash
docker build -t log-committer-tcp:latest .
```

The Dockerfile uses a **multi-stage build**:

| Stage | Base | Purpose |
|---|---|---|
| `builder` | `ubuntu:24.04` + build-essential + cmake | Compiles the binary with LTO |
| `runtime` | `ubuntu:24.04` (minimal) | Ships only the binary; no compiler toolchain |

### Run a single node

```bash
docker run --rm -p 9090:9090 \
  -e TCP_HOST=0.0.0.0 \
  -e TCP_PORT=9090 \
  -e THREAD_COUNT=4 \
  log-committer-tcp:latest
```

### Run a primary + replica pair with Docker Compose

```bash
# Start both nodes
docker compose up -d

# Tail logs from both
docker compose logs -f

# Stop everything
docker compose down
```

Node addresses inside the `log_net` network:

| Service | Internal address | Host port |
|---|---|---|
| Primary | `log-committer-primary:9090` | `localhost:9090` |
| Secondary | `log-committer-secondary:9091` | `localhost:9091` |

### Environment variables consumed by the container

| Variable | Default | Description |
|---|---|---|
| `TCP_HOST` | `0.0.0.0` | Bind address |
| `TCP_PORT` | `9090` | Listen port |
| `THREAD_COUNT` | `4` | Worker thread count |

> **Note:** The `main.cpp` demo reads these from `std::getenv`. Wire them into your own `main()` the same way, or replace with a config-file loader.

### Kubernetes (quick-start)

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: log-committer-tcp
spec:
  replicas: 3
  selector:
    matchLabels:
      app: log-committer-tcp
  template:
    metadata:
      labels:
        app: log-committer-tcp
    spec:
      containers:
      - name: server
        image: log-committer-tcp:latest
        ports:
        - containerPort: 9090
          protocol: TCP
        env:
        - name: THREAD_COUNT
          value: "8"
        readinessProbe:
          tcpSocket:
            port: 9090
          initialDelaySeconds: 3
          periodSeconds: 5
---
apiVersion: v1
kind: Service
metadata:
  name: log-committer-tcp
spec:
  selector:
    app: log-committer-tcp
  ports:
  - port: 9090
    targetPort: 9090
  type: ClusterIP
```

---

## Integrating Into Your Application

Only `include/tcp_server.h` is part of the public API. Everything else is an implementation detail.

### Minimal example

```cpp
#include "tcp_server.h"

int main() {
    log_committer::TcpServer server("0.0.0.0", 9090, /*threads=*/4);

    server.on_message([](const log_committer::Message& msg) {
        // msg.client_id  — unique uint32 per connection
        // msg.payload    — std::vector<uint8_t>
        // msg.payload_str() — convenience cast to std::string
        std::cout << "Got: " << msg.payload_str() << "\n";
    });

    server.start(); // blocks until stop() is called from another thread
}
```

### Full log-committer integration

```cpp
#include "tcp_server.h"
#include "wal.h"          // your WAL implementation
#include "replicator.h"   // your replication layer

class LogCommitterNode {
public:
    LogCommitterNode(uint16_t port, Wal& wal, Replicator& repl)
        : server_("0.0.0.0", port, /*threads=*/8)
        , wal_(wal)
        , repl_(repl)
    {
        server_
            .on_connect([this](uint32_t id, const std::string& addr) {
                sessions_.emplace(id, Session{ .remote = addr });
                repl_.notify_writer_connected(id);
            })
            .on_disconnect([this](uint32_t id) {
                sessions_.erase(id);
                repl_.notify_writer_disconnected(id);
            })
            .on_message([this](const log_committer::Message& msg) {
                handle_log_entry(msg);
            });
    }

    void run() {
        server_.start_async();          // IO loop in background thread
        repl_.run_until_shutdown();     // your replication event loop
        server_.stop();
    }

private:
    struct Session { std::string remote; };

    void handle_log_entry(const log_committer::Message& msg) {
        // 1. Deserialise (protobuf / FlatBuffers / raw)
        auto entry = LogEntry::parse(msg.payload);

        // 2. Validate sequence number / epoch
        if (!sessions_.count(msg.client_id)) return;

        // 3. Append to WAL (synchronous fsync or async depending on durability SLA)
        uint64_t lsn = wal_.append(entry);

        // 4. Replicate and wait for quorum ack (or fire-and-forget)
        repl_.replicate(lsn, entry);

        // 5. Ack the writer with the committed LSN
        std::string ack = "ACK:" + std::to_string(lsn);
        server_.send(msg.client_id, ack);
    }

    log_committer::TcpServer server_;
    Wal&        wal_;
    Replicator& repl_;
    std::unordered_map<uint32_t, Session> sessions_;  // guarded by thread pool
};
```

### Sending responses back

```cpp
// Unicast — reply to a specific writer
server.send(msg.client_id, "ACK:42");

// Raw bytes (e.g. serialised protobuf)
std::vector<uint8_t> pb_bytes = response.SerializeAsString() | ...;
server.send(msg.client_id, pb_bytes.data(), pb_bytes.size());

// Broadcast to all connected writers (e.g. leader change notification)
server.broadcast("LEADER_CHANGE:node-2");
```

`send()` is thread-safe and can be called from any thread, including the `on_message` callback thread pool.

### Broadcasting

Useful for push-notifications from the committer back to all writers:

```cpp
// After a leader election resolves:
server.broadcast("NEW_LEADER:" + new_leader_addr);

// After a checkpoint:
server.broadcast("CHECKPOINT:" + std::to_string(checkpoint_lsn));
```

---

## Configuration Reference

### `TcpServer` constructor

```cpp
TcpServer(const std::string& host,
          uint16_t           port,
          size_t             thread_count = 4);
```

| Parameter | Description |
|---|---|
| `host` | Bind address. Use `"0.0.0.0"` for all interfaces or a specific IP to bind to one NIC. |
| `port` | TCP port to listen on. |
| `thread_count` | Number of worker threads in the pool. A good starting point is `2 × CPU cores` for IO-bound commit handlers, or equal to core count for CPU-bound handlers. |

### Compile-time tunables (in `connection.cpp`)

| Constant | Default | Description |
|---|---|---|
| `HEADER_SIZE` | `4` bytes | Length prefix width. Changing this requires matching client changes. |
| Max payload guard | `64 MiB` | Connections sending larger frames are dropped. Tune to your max log entry size. |

### Epoll tunables (in `tcp_server.cpp`)

| Constant | Default | Description |
|---|---|---|
| `MAX_EVENTS` | `128` | Batch size for `epoll_wait`. Increase if you expect thousands of simultaneous active clients. |
| `SOMAXCONN` | System default (~4096) | Passed to `listen()`. |

---

## Thread Safety Contract

| Operation | Thread-safe? | Notes |
|---|---|---|
| `on_message` / `on_connect` / `on_disconnect` callbacks | ✅ | Called from thread pool; concurrent with each other |
| `server.send()` | ✅ | Can be called from callback threads or any other thread |
| `server.broadcast()` | ✅ | Same as `send` |
| `server.stop()` | ✅ | Safe to call from signal handler via `std::atomic` flag |
| `server.connected_clients()` | ✅ | Lock-protected |
| Registering callbacks (`on_message`, etc.) | ❌ | Must be done **before** `start()` / `start_async()` |

---

## Error Handling

The server handles these failure modes silently and closes the offending connection:

- Client disconnects mid-stream (partial header or payload)
- Client sends a payload length of 0 or > 64 MiB
- `write()` fails with anything other than `EAGAIN`
- `epoll` reports `EPOLLERR` or `EPOLLHUP`

Your `on_disconnect` callback fires in all cases, so you can clean up session state regardless of how the connection ends.

The server itself does **not** throw after `start()` / `start_async()` — all errors are logged to `stderr`. If you need structured error reporting, replace the `std::cerr` calls in `tcp_server.cpp` with your logging framework (spdlog, glog, etc.).

---

## Extending the Server

### Swap in protobuf framing

Replace the raw `std::vector<uint8_t>` payload with a protobuf parse in your `on_message` handler:

```cpp
server.on_message([&](const log_committer::Message& msg) {
    MyProto::LogEntry entry;
    if (!entry.ParseFromArray(msg.payload.data(), msg.payload.size())) {
        // malformed — optionally send an error frame back
        return;
    }
    // ... commit entry
});
```

### Add TLS (via OpenSSL / BoringSSL)

Wrap `Connection::on_readable` / `on_writable` with an SSL layer by adding an `SSL*` member to `Connection` and replacing the raw `::read` / `::write` calls with `SSL_read` / `SSL_write`. The epoll loop does not need to change.

### Rate limiting per client

In `accept_new_connection()`, track connection counts per source IP and `close()` the fd immediately if the limit is exceeded — before the `Connection` object is created.

---

## Troubleshooting

**`bind() failed: Address already in use`**
The port is still held by a previous process. Either wait for `TIME_WAIT` to expire (~60 s) or set `SO_REUSEPORT` (already enabled). Kill the previous process with `fuser -k 9090/tcp`.

**`epoll_create1 failed`**
Check the process's file descriptor limit: `ulimit -n`. For high connection counts set it to at least `65535`. In Docker, add `--ulimit nofile=65535:65535`.

**Messages arrive out of order in the callback**
By design — the thread pool dispatches callbacks concurrently. If your log committer requires per-client ordering, add a per-`client_id` strand (a single-threaded queue keyed by `client_id`).

**High latency under load**
- Increase `thread_count` if CPU utilisation in the pool workers is high.
- If the bottleneck is the WAL `fsync`, consider async writes with a flush interval and batch acking.
- Profile with `perf record -g ./log_committer_tcp` to confirm where time is spent.