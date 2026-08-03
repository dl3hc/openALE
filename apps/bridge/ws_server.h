/**
 * \file apps/bridge/ws_server.h
 * \brief Minimal single-client RFC6455 WebSocket server.
 *
 * Threading:
 *   - A dedicated I/O thread owns ALL socket operations (accept, recv, send,
 *     close) via a WSAPoll()/poll()-based event loop (5 ms timeout). It can
 *     serve concurrent plain-HTTP GETs (browser cold-load: index.html, app.js,
 *     styles.css) and the WebSocket session simultaneously — no more blocking
 *     in handle_client() that prevented accept() from running.
 *   - send_text()/send_binary() push frames into a mutex-protected send_queue_;
 *     the I/O thread drains it on every poll iteration (≤5 ms latency).
 *   - Received text frames are pushed into recv_queue_ (mutex-protected);
 *     the main loop drains it non-blockingly via pop_message().
 *   - stop() sets running_=false, joins the I/O thread (which closes all
 *     sockets in its cleanup section), then calls WSACleanup.  No socket is
 *     closed before join() — eliminates the close-before-send race present in
 *     the prior blocking design.  See docs/THREADING.md.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace bridge {

class WsServer {
public:
    WsServer() = default;
    ~WsServer();

    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;

    /**
     * Start listening on \p port and spawn the I/O thread.
     * \p bind_remote — false (default): bind to 127.0.0.1 (localhost only);
     *                  true: bind to 0.0.0.0 (all interfaces, LAN-reachable).
     */
    bool start(uint16_t port, bool bind_remote = false);

    /**
     * Directory the I/O thread serves static files from for plain HTTP GETs.
     * Must be set before start().  Read-only after start().
     */
    void set_web_root(std::string root) { web_root_ = std::move(root); }

    /** Stop the I/O thread (join first) and release all resources. */
    void stop();

    /** Non-blocking dequeue of one received text-frame payload. */
    bool pop_message(std::string& out);

    /** Non-blocking dequeue of one received binary-frame payload (e.g. voice
     *  mic PCM uplink from the browser). Returns the raw frame bytes. */
    bool pop_binary(std::vector<uint8_t>& out);

    /** Enqueue a text frame for the I/O thread to send (≤5 ms latency). */
    bool send_text(const std::string& payload);

    /** Enqueue a binary frame for the I/O thread to send (≤5 ms latency). */
    bool send_binary(const void* data, size_t len);

    bool is_connected() const;

private:
    using SocketHandle = intptr_t;
    static constexpr SocketHandle kInvalid        = -1;
    static constexpr int          kMaxPendingHttp = 8;
    static constexpr size_t       kSendQueueLimit = 32;  // drop-oldest overflow guard

    struct SendItem {
        uint8_t              opcode;
        std::vector<uint8_t> data;
    };

    struct PendingHttp {
        SocketHandle fd = kInvalid;
        std::string  buf;              // accumulated HTTP request bytes
    };

    void io_thread_main(uint16_t port);
    bool parse_ws_frames_(SocketHandle ws_handle);  // I/O thread only

    std::string               web_root_;
    bool                      bind_remote_ = false;
    SocketHandle              listen_sock_ = kInvalid;
    std::atomic<SocketHandle> client_{kInvalid};   // kInvalid = not connected
    std::thread               io_thread_;
    std::atomic<bool>         running_{false};

    // Cross-thread queues (mutex-protected):
    std::mutex           send_queue_mtx_;
    std::queue<SendItem> send_queue_;

    std::mutex               recv_mutex_;
    std::queue<std::string>  recv_queue_;
    std::queue<std::vector<uint8_t>> recv_binary_queue_;  // voice mic uplink, etc.

    // I/O-thread-private state — no locking needed (only io_thread_main touches these):
    PendingHttp pending_http_[kMaxPendingHttp];
    std::string ws_recv_buf_;       // WS frame accumulation buffer
    std::string ws_frag_acc_;       // reassembly buffer for fragmented messages
    uint8_t     ws_frag_opcode_ = 0;  // 0 = no fragmented message in progress
    uint32_t    ws_reject_count_ = 0; // consecutive upgrade rejections; resets on connect
};

} // namespace bridge
