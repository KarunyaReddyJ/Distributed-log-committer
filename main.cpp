#include "tcp-server/tcp_server.h"
#include <iostream>
#include <csignal>
#include <atomic>

// ─── Stub: your log-committer core ───────────────────────────────────────────
namespace log_committer {

void commit_log_entry(uint32_t client_id, const std::string& entry) {
    // TODO: validate, replicate, flush to WAL, reply with ack
    std::cout << "[LogCommitter] commit from client #" << client_id
              << " | entry=" << entry << "\n";
}

} // namespace log_committer

// ─── Signal handling ──────────────────────────────────────────────────────────
static std::atomic<bool> g_shutdown { false };

int main() {
    using namespace log_committer;

    // Build server
    TcpServer server("0.0.0.0", 9090, /*threads=*/4);

    server
        .on_connect([](uint32_t id, const std::string& addr) {
            std::cout << "[App] New log-writer connected: id=" << id
                      << " addr=" << addr << "\n";
        })
        .on_disconnect([](uint32_t id) {
            std::cout << "[App] Log-writer disconnected: id=" << id << "\n";
        })
        .on_message([&server](const Message& msg) {
            // Payload is a log entry (plain text in this example;
            // swap for protobuf / FlatBuffers in production)
            commit_log_entry(msg.client_id, msg.payload_str());

            // Echo ack back to the writer
            server.send(msg.client_id, "ACK");
        });

    // Graceful shutdown on SIGINT / SIGTERM
    std::signal(SIGINT,  [](int) { g_shutdown = true; });
    std::signal(SIGTERM, [](int) { g_shutdown = true; });

    server.start_async();
    std::cout << "[App] Log-committer TCP module running. Ctrl-C to stop.\n";

    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    std::cout << "\n[App] Shutting down...\n";
    server.stop();
    return 0;
}
