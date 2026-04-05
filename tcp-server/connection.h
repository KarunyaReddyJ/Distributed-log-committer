#pragma once

#include <vector>
#include <deque>
#include <string>
#include <cstdint>
#include <mutex>

namespace log_committer {

// Per-client connection state.
// All fields are touched only from the epoll IO thread EXCEPT
// the write_queue_ which is protected by write_mutex_ so that
// TcpServer::send() can be called from any thread.

class Connection {
public:
    enum class ReadState { HEADER, PAYLOAD };

    explicit Connection(int fd, uint32_t client_id, std::string remote_addr);

    int         fd()         const { return fd_; }
    uint32_t    client_id()  const { return client_id_; }
    const std::string& remote_addr() const { return remote_addr_; }
    bool        want_write() const;

    // Called from the IO thread when epoll says readable.
    // Returns true if a complete message was assembled in `out_payload`.
    bool on_readable(std::vector<uint8_t>& out_payload);

    // Enqueue outbound data (thread-safe).
    void enqueue_write(const uint8_t* data, size_t len);

    // Called from the IO thread when epoll says writable.
    // Returns true if all pending data has been flushed.
    bool on_writable();

private:
    static constexpr size_t HEADER_SIZE = 4;

    int         fd_;
    uint32_t    client_id_;
    std::string remote_addr_;

    // ── Read side ────────────────────────────────────────────────────────────
    ReadState            read_state_ { ReadState::HEADER };
    uint8_t              hdr_buf_[HEADER_SIZE];
    size_t               hdr_bytes_read_ { 0 };
    uint32_t             payload_len_    { 0 };
    std::vector<uint8_t> payload_buf_;
    size_t               payload_bytes_read_ { 0 };

    // ── Write side ───────────────────────────────────────────────────────────
    mutable std::mutex          write_mutex_;
    std::deque<std::vector<uint8_t>> write_queue_;
    size_t                      write_offset_ { 0 }; // offset into front chunk
};

} // namespace log_committer
