#include "connection.h"

#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>   // ntohl / htonl

namespace log_committer {

Connection::Connection(int fd, uint32_t client_id, std::string remote_addr)
    : fd_(fd), client_id_(client_id), remote_addr_(std::move(remote_addr)) {}

// ── Read ─────────────────────────────────────────────────────────────────────

bool Connection::on_readable(std::vector<uint8_t>& out_payload) {
    // Phase 1: read 4-byte header
    if (read_state_ == ReadState::HEADER) {
        while (hdr_bytes_read_ < HEADER_SIZE) {
            ssize_t n = ::read(fd_,
                               hdr_buf_ + hdr_bytes_read_,
                               HEADER_SIZE - hdr_bytes_read_);
            if (n <= 0) {
                if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                    throw std::runtime_error("client disconnected");
                return false; // EAGAIN – come back later
            }
            hdr_bytes_read_ += static_cast<size_t>(n);
        }

        // Decode big-endian length
        uint32_t net_len;
        std::memcpy(&net_len, hdr_buf_, 4);
        payload_len_ = ntohl(net_len);

        if (payload_len_ == 0 || payload_len_ > 64 * 1024 * 1024 /* 64 MiB guard */) {
            throw std::runtime_error("invalid payload length: " + std::to_string(payload_len_));
        }

        payload_buf_.resize(payload_len_);
        payload_bytes_read_ = 0;
        read_state_ = ReadState::PAYLOAD;
    }

    // Phase 2: read payload
    while (payload_bytes_read_ < payload_len_) {
        ssize_t n = ::read(fd_,
                           payload_buf_.data() + payload_bytes_read_,
                           payload_len_ - payload_bytes_read_);
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK))
                throw std::runtime_error("client disconnected during payload");
            return false;
        }
        payload_bytes_read_ += static_cast<size_t>(n);
    }

    // Full message assembled
    out_payload = std::move(payload_buf_);
    // Reset for next message
    hdr_bytes_read_     = 0;
    payload_len_        = 0;
    payload_bytes_read_ = 0;
    read_state_         = ReadState::HEADER;
    return true;
}

// ── Write ─────────────────────────────────────────────────────────────────────

bool Connection::want_write() const {
    std::lock_guard<std::mutex> lock(write_mutex_);
    return !write_queue_.empty();
}

void Connection::enqueue_write(const uint8_t* data, size_t len) {
    // Build a framed buffer: [4-byte big-endian length][payload]
    std::vector<uint8_t> frame(HEADER_SIZE + len);
    uint32_t net_len = htonl(static_cast<uint32_t>(len));
    std::memcpy(frame.data(), &net_len, HEADER_SIZE);
    std::memcpy(frame.data() + HEADER_SIZE, data, len);

    std::lock_guard<std::mutex> lock(write_mutex_);
    write_queue_.push_back(std::move(frame));
}

bool Connection::on_writable() {
    std::lock_guard<std::mutex> lock(write_mutex_);

    while (!write_queue_.empty()) {
        auto& chunk = write_queue_.front();
        while (write_offset_ < chunk.size()) {
            ssize_t n = ::write(fd_,
                                chunk.data() + write_offset_,
                                chunk.size() - write_offset_);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return false; // still pending
                throw std::runtime_error("write error: " + std::string(std::strerror(errno)));
            }
            write_offset_ += static_cast<size_t>(n);
        }
        write_queue_.pop_front();
        write_offset_ = 0;
    }
    return true; // all flushed
}

} // namespace log_committer
