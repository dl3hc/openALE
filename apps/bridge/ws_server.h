/**
 * \file apps/bridge/ws_server.h
 * \brief Minimal single-client RFC6455 WebSocket server.
 *
 * Socket bootstrap mirrors apps/radio_mock.cpp (WSAStartup/socket/bind/listen/
 * accept, single client at a time — "ALE is single-station" applies here too:
 * one operator GUI per running bridge).
 *
 * Threading:
 *   - A dedicated I/O thread owns accept()+recv(), static-file serving for
 *     plain HTTP GETs, and the WS handshake. It never calls send_text()/
 *     send_binary() — those are called by the caller's thread. Today every
 *     send originates on the main loop (command replies, async events, AND the
 *     spectrum binary frames: ALEController's spectrum callback fires inline
 *     from feed_audio() on the main loop, not from the WASAPI audio thread).
 *     send_text()/send_binary() are still mutex-serialised so a future
 *     off-main-thread sender stays wire-safe. See docs/THREADING.md.
 *   - Received text-frame payloads are pushed into a mutex-protected queue;
 *     the main loop drains it non-blockingly via pop_message(), mirroring
 *     ale_cli.cpp's stdin_reader -> g_cmd_queue pattern.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace bridge {

class WsServer {
public:
    WsServer() = default;
    ~WsServer();

    WsServer(const WsServer&) = delete;
    WsServer& operator=(const WsServer&) = delete;

    /** Start listening on \p port and spawn the I/O thread. */
    bool start(uint16_t port);

    /**
     * Directory the I/O thread serves static files from for plain HTTP GETs
     * (the apps/gui/ web root — index.html / app.js / styles.css). Must be set
     * before start(), or left empty to disable static serving (404 everything).
     * Read-only after start(): only the I/O thread reads it, no locking needed.
     */
    void set_web_root(std::string root) { web_root_ = std::move(root); }

    /** Stop the I/O thread and close all sockets. Safe to call if not started. */
    void stop();

    /** Non-blocking dequeue of one received text-frame payload. */
    bool pop_message(std::string& out);

    /** Send a text frame. False (silently dropped) if no client is connected. */
    bool send_text(const std::string& payload);

    /** Send a binary frame. False (silently dropped) if no client is connected. */
    bool send_binary(const void* data, size_t len);

    bool is_connected() const;

private:
    using SocketHandle = intptr_t;
    static constexpr SocketHandle kInvalid = -1;

    void io_thread_main(uint16_t port);
    bool handle_client(SocketHandle client);
    bool send_frame(uint8_t opcode, const uint8_t* data, size_t len);

    std::string  web_root_;   // static-file root for plain HTTP GETs (set before start())
    SocketHandle listen_sock_ = kInvalid;
    std::atomic<SocketHandle> client_{kInvalid};
    std::thread io_thread_;
    std::atomic<bool> running_{false};

    std::mutex send_mutex_;

    std::mutex recv_mutex_;
    std::queue<std::string> recv_queue_;
};

} // namespace bridge
