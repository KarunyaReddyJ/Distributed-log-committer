#include "tcp_server.h"
#include "connection.h"
#include "thread_pool.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <iostream>

namespace log_committer {

// ─── Helpers ──────────────────────────────────────────────────────────────────

static void set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) throw std::runtime_error("fcntl F_GETFL failed");
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        throw std::runtime_error("fcntl F_SETFL O_NONBLOCK failed");
}

static void set_socket_opts(int fd) {
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET,   SO_REUSEADDR, &opt, sizeof(opt));
    ::setsockopt(fd, SOL_SOCKET,   SO_REUSEPORT, &opt, sizeof(opt));
    ::setsockopt(fd, IPPROTO_TCP,  TCP_NODELAY,  &opt, sizeof(opt));
}

// ─── Constructor / Destructor ─────────────────────────────────────────────────

TcpServer::TcpServer(const std::string& host,
                     uint16_t           port,
                     size_t             thread_count)
    : host_(host), port_(port)
{
    thread_pool_ = std::make_unique<ThreadPool>(thread_count);
}

TcpServer::~TcpServer() {
    stop();
}

// ─── Callback registration ────────────────────────────────────────────────────

TcpServer& TcpServer::on_message(OnMessageCallback cb) {
    on_message_ = std::move(cb); return *this;
}
TcpServer& TcpServer::on_connect(OnConnectCallback cb) {
    on_connect_ = std::move(cb); return *this;
}
TcpServer& TcpServer::on_disconnect(OnDisconnectCallback cb) {
    on_disconnect_ = std::move(cb); return *this;
}

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void TcpServer::start() {
    bind_and_listen();
    run_event_loop(); // blocks
}

void TcpServer::start_async() {
    bind_and_listen();
    io_thread_ = std::thread([this] { run_event_loop(); });
}

void TcpServer::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) return;

    // Wake up epoll by writing to the pipe
    if (notify_pipe_[1] >= 0) {
        uint8_t byte = 1;
        { auto _r = ::write(notify_pipe_[1], &byte, 1); (void)_r; }
    }
    if (io_thread_.joinable()) io_thread_.join();

    if (epoll_fd_ >= 0)  { ::close(epoll_fd_);  epoll_fd_  = -1; }
    if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
    if (notify_pipe_[0] >= 0) { ::close(notify_pipe_[0]); notify_pipe_[0] = -1; }
    if (notify_pipe_[1] >= 0) { ::close(notify_pipe_[1]); notify_pipe_[1] = -1; }

    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (auto& [fd, conn] : connections_) ::close(fd);
    connections_.clear();
}

// ─── Bind + Listen ────────────────────────────────────────────────────────────

void TcpServer::bind_and_listen() {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0)
        throw std::runtime_error("socket() failed: " + std::string(std::strerror(errno)));

    set_socket_opts(listen_fd_);
    set_nonblocking(listen_fd_);

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0)
        throw std::runtime_error("invalid host address: " + host_);

    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed: " + std::string(std::strerror(errno)));

    if (::listen(listen_fd_, SOMAXCONN) < 0)
        throw std::runtime_error("listen() failed: " + std::string(std::strerror(errno)));

    // epoll
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ < 0)
        throw std::runtime_error("epoll_create1 failed");

    // Notification pipe (used by stop())
    if (::pipe2(notify_pipe_, O_NONBLOCK | O_CLOEXEC) < 0)
        throw std::runtime_error("pipe2 failed");
    add_to_epoll(notify_pipe_[0], EPOLLIN);

    add_to_epoll(listen_fd_, EPOLLIN);

    running_ = true;
    std::cout << "[TcpServer] Listening on " << host_ << ":" << port_
              << " | threads=" << thread_pool_->size() << "\n";
}

// ─── epoll helpers ────────────────────────────────────────────────────────────

void TcpServer::add_to_epoll(int fd, uint32_t events) {
    epoll_event ev {};
    ev.events  = events | EPOLLET; // edge-triggered
    ev.data.fd = fd;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0)
        throw std::runtime_error("epoll_ctl ADD failed: " + std::string(std::strerror(errno)));
}

void TcpServer::modify_epoll(int fd, uint32_t events) {
    epoll_event ev {};
    ev.events  = events | EPOLLET;
    ev.data.fd = fd;
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
}

void TcpServer::remove_from_epoll(int fd) {
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
}

// ─── Event loop ───────────────────────────────────────────────────────────────

void TcpServer::run_event_loop() {
    constexpr int MAX_EVENTS = 128;
    epoll_event events[MAX_EVENTS];

    while (running_) {
        int n = ::epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == notify_pipe_[0]) {
                // stop() was called
                return;
            } else if (fd == listen_fd_) {
                accept_new_connection();
            } else {
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    close_connection(fd);
                    continue;
                }
                if (events[i].events & EPOLLIN)  handle_readable(fd);
                if (events[i].events & EPOLLOUT) handle_writable(fd);
            }
        }
    }
}

// ─── Accept ───────────────────────────────────────────────────────────────────

void TcpServer::accept_new_connection() {
    // Loop until EAGAIN (edge-triggered)
    for (;;) {
        sockaddr_in peer_addr {};
        socklen_t peer_len = sizeof(peer_addr);
        int client_fd = ::accept4(listen_fd_,
                                  reinterpret_cast<sockaddr*>(&peer_addr),
                                  &peer_len,
                                  SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            std::cerr << "[TcpServer] accept4 error: " << std::strerror(errno) << "\n";
            break;
        }

        char ip_buf[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &peer_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::string remote = std::string(ip_buf) + ":" + std::to_string(ntohs(peer_addr.sin_port));

        uint32_t cid = next_client_id_.fetch_add(1, std::memory_order_relaxed);
        auto conn = std::make_shared<Connection>(client_fd, cid, remote);

        {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            connections_[client_fd] = conn;
        }

        add_to_epoll(client_fd, EPOLLIN | EPOLLOUT);

        std::cout << "[TcpServer] Client #" << cid << " connected from " << remote << "\n";
        if (on_connect_) {
            // Fire in the thread pool so we don't block the IO loop
            thread_pool_->enqueue([this, cid, remote] {
                on_connect_(cid, remote);
            });
        }
    }
}

// ─── Read ─────────────────────────────────────────────────────────────────────

void TcpServer::handle_readable(int fd) {
    std::shared_ptr<Connection> conn;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        auto it = connections_.find(fd);
        if (it == connections_.end()) return;
        conn = it->second;
    }

    // Edge-triggered: drain the socket completely
    for (;;) {
        std::vector<uint8_t> payload;
        try {
            bool complete = conn->on_readable(payload);
            if (!complete) break; // EAGAIN — come back on next epoll event

            // Full message — hand off to thread pool
            Message msg;
            msg.client_id = conn->client_id();
            msg.payload   = std::move(payload);

            if (on_message_) {
                thread_pool_->enqueue([this, m = std::move(msg)]() mutable {
                    on_message_(m);
                });
            }
        } catch (const std::exception& e) {
            std::cerr << "[TcpServer] Read error on fd=" << fd
                      << " (" << e.what() << "), closing.\n";
            close_connection(fd);
            return;
        }
    }

    // Re-arm: still interested in writes if there's pending data
    uint32_t ev = EPOLLIN;
    if (conn->want_write()) ev |= EPOLLOUT;
    modify_epoll(fd, ev);
}

// ─── Write ────────────────────────────────────────────────────────────────────

void TcpServer::handle_writable(int fd) {
    std::shared_ptr<Connection> conn;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        auto it = connections_.find(fd);
        if (it == connections_.end()) return;
        conn = it->second;
    }

    try {
        bool flushed = conn->on_writable();
        // If all written, stop watching EPOLLOUT (reduces wake-ups)
        uint32_t out_ev = EPOLLIN;
        if (!flushed) out_ev |= static_cast<uint32_t>(EPOLLOUT);
        modify_epoll(fd, out_ev);
    } catch (const std::exception& e) {
        std::cerr << "[TcpServer] Write error on fd=" << fd
                  << " (" << e.what() << "), closing.\n";
        close_connection(fd);
    }
}

// ─── Close ────────────────────────────────────────────────────────────────────

void TcpServer::close_connection(int fd) {
    uint32_t cid = 0;
    {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        auto it = connections_.find(fd);
        if (it == connections_.end()) return;
        cid = it->second->client_id();
        connections_.erase(it);
    }
    remove_from_epoll(fd);
    ::close(fd);

    std::cout << "[TcpServer] Client #" << cid << " disconnected.\n";
    if (on_disconnect_) {
        thread_pool_->enqueue([this, cid] { on_disconnect_(cid); });
    }
}

// ─── Send / Broadcast ─────────────────────────────────────────────────────────

bool TcpServer::send(uint32_t client_id, const std::string& data) {
    return send(client_id,
                reinterpret_cast<const uint8_t*>(data.data()),
                data.size());
}

bool TcpServer::send(uint32_t client_id, const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (auto& [fd, conn] : connections_) {
        if (conn->client_id() == client_id) {
            conn->enqueue_write(data, len);
            // Wake up epoll write interest
            modify_epoll(fd, EPOLLIN | EPOLLOUT);
            return true;
        }
    }
    return false; // client not found
}

void TcpServer::broadcast(const std::string& data) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (auto& [fd, conn] : connections_) {
        conn->enqueue_write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        modify_epoll(fd, EPOLLIN | EPOLLOUT);
    }
}

size_t TcpServer::connected_clients() const {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    return connections_.size();
}

} // namespace log_committer
