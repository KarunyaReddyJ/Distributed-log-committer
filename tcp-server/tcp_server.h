#pragma once

#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <unordered_map>
#include <cstdint>

namespace log_committer {

// ─── Forward declarations ────────────────────────────────────────────────────

class Connection;
class ThreadPool;
class TcpServer;

// ─── Message (length-prefixed frame) ─────────────────────────────────────────

struct Message {
    uint32_t  client_id;
    std::vector<uint8_t> payload;   // raw bytes after the 4-byte length header

    std::string payload_str() const {
        return std::string(payload.begin(), payload.end());
    }
};

// ─── Callbacks the server owner supplies ─────────────────────────────────────

using OnMessageCallback  = std::function<void(const Message&)>;
using OnConnectCallback  = std::function<void(uint32_t client_id, const std::string& remote_addr)>;
using OnDisconnectCallback = std::function<void(uint32_t client_id)>;

// ─── TcpServer ────────────────────────────────────────────────────────────────
//
// Usage:
//   TcpServer server("0.0.0.0", 9090, /*threads=*/4);
//   server.on_message([](const Message& m){ /* handle */ });
//   server.on_connect([](uint32_t id, const std::string& addr){ });
//   server.on_disconnect([](uint32_t id){ });
//   server.start();          // blocking; returns when server shuts down
//   // OR
//   server.start_async();    // non-blocking; call server.stop() later

class TcpServer {
public:
    explicit TcpServer(const std::string& host,
                       uint16_t           port,
                       size_t             thread_count = 4);
    ~TcpServer();

    // ── Registration ──────────────────────────────────────────────────────────
    TcpServer& on_message   (OnMessageCallback cb);
    TcpServer& on_connect   (OnConnectCallback cb);
    TcpServer& on_disconnect(OnDisconnectCallback cb);

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    void start();        // blocking
    void start_async();  // non-blocking; runs epoll loop in background thread
    void stop();

    // ── Send helpers ─────────────────────────────────────────────────────────
    bool send(uint32_t client_id, const std::string& data);
    bool send(uint32_t client_id, const uint8_t* data, size_t len);
    void broadcast(const std::string& data);

    // ── Stats ─────────────────────────────────────────────────────────────────
    size_t connected_clients() const;

private:
    // Setup
    void bind_and_listen();
    void run_event_loop();

    // epoll helpers
    void add_to_epoll(int fd, uint32_t events);
    void modify_epoll(int fd, uint32_t events);
    void remove_from_epoll(int fd);

    // Connection management
    void accept_new_connection();
    void handle_readable(int fd);
    void handle_writable(int fd);
    void close_connection(int fd);

    // Wire format
    static constexpr size_t HEADER_SIZE = 4; // uint32_t payload length, big-endian

    std::string  host_;
    uint16_t     port_;
    int          listen_fd_  { -1 };
    int          epoll_fd_   { -1 };
    int          notify_pipe_[2] { -1, -1 }; // used to wake up epoll on stop()

    std::atomic<bool>     running_   { false };
    std::thread           io_thread_;

    OnMessageCallback    on_message_;
    OnConnectCallback    on_connect_;
    OnDisconnectCallback on_disconnect_;

    std::unique_ptr<ThreadPool> thread_pool_;

    // fd → Connection mapping (accessed only from the IO thread)
    std::unordered_map<int, std::shared_ptr<Connection>> connections_;
    mutable std::mutex conn_mutex_; // guards connections_ for send/broadcast

    std::atomic<uint32_t> next_client_id_ { 1 };
};

} // namespace log_committer
