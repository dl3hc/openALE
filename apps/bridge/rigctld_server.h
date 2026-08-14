/**
 * \file apps/bridge/rigctld_server.h
 * \brief Multi-client rigctld/netrigctl-compatible read-only TCP server.
 *
 * Threading:
 *   - A dedicated I/O thread owns ALL socket operations (accept, recv, send,
 *     close) via a WSAPoll()/poll()-based event loop (5 ms timeout), fanning
 *     out over up to kMaxClients simultaneous persistent connections —
 *     unlike WsServer (single GUI session), this must serve several
 *     long-lived netrigctl clients at once.
 *   - Received lines are split on '\n' and pushed into recv_queue_ (mutex-
 *     protected), tagged with a per-connection id; the main loop drains them
 *     via pop_request() and answers via send_reply().
 *   - This class never touches ALEController or pal::IRadio. Protocol
 *     interpretation and radio-state reads happen only on the main thread
 *     (see docs/THREADING.md — ALEController is single-owner/main-thread-
 *     only, and RIG_CONNECT/RIG_DISCONNECT reset the radio pointer with no
 *     synchronization, so a foreign thread must never hold it).
 *   - stop() sets running_=false, joins the I/O thread (which closes every
 *     socket in its cleanup section), mirroring WsServer::stop() — no socket
 *     is closed before join() to avoid a close-before-send race.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

namespace bridge {

class RigctldServer {
public:
    RigctldServer() = default;
    ~RigctldServer();

    RigctldServer(const RigctldServer&) = delete;
    RigctldServer& operator=(const RigctldServer&) = delete;

    /**
     * Start listening on \p port and spawn the I/O thread.
     * \p bind_remote — false (default): bind to 127.0.0.1 (localhost only);
     *                  true: bind to 0.0.0.0 (all interfaces, LAN-reachable).
     * Mirrors WsServer::start()'s parameter shape/semantics.
     */
    bool start(uint16_t port, bool bind_remote = false);

    /** Stop the I/O thread (join first) and release all resources. */
    void stop();

    /** Non-blocking dequeue of one received (connection id, line) pair. */
    bool pop_request(uint64_t& conn_id, std::string& line);

    /** Queue a reply for a specific connection. No-op if already closed. */
    bool send_reply(uint64_t conn_id, const std::string& text);

    bool is_running() const { return running_.load(); }
    size_t client_count() const { return client_count_.load(); }

private:
    using SocketHandle = intptr_t;
    static constexpr SocketHandle kInvalid    = -1;
    static constexpr int          kMaxClients = 16;

    struct ClientSlot {
        SocketHandle fd = kInvalid;
        std::string  recv_buf;   // I/O-thread-private, accumulates until '\n'
        uint64_t     id = 0;
    };

    void io_thread_main();

    bool         bind_remote_ = false;
    SocketHandle listen_sock_ = kInvalid;
    std::thread  io_thread_;
    std::atomic<bool>     running_{false};
    std::atomic<uint64_t> next_conn_id_{1};
    std::atomic<size_t>   client_count_{0};

    // Cross-thread queues (mutex-protected):
    std::mutex recv_mtx_;
    std::queue<std::pair<uint64_t, std::string>> recv_queue_;

    std::mutex send_mtx_;
    std::queue<std::pair<uint64_t, std::string>> send_queue_;

    // I/O-thread-private state — no locking needed (only io_thread_main touches this):
    ClientSlot slots_[kMaxClients];
};

} // namespace bridge
