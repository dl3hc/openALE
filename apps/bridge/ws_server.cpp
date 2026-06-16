/**
 * \file apps/bridge/ws_server.cpp
 */

#include "ws_server.h"
#include "ws_handshake.h"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using raw_sock_t = SOCKET;
   static constexpr raw_sock_t kRawInvalid = INVALID_SOCKET;
   static void close_sock(raw_sock_t s) { closesocket(s); }
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
   using raw_sock_t = int;
   static constexpr raw_sock_t kRawInvalid = -1;
   static void close_sock(raw_sock_t s) { close(s); }
#endif

#include <cstdio>
#include <cstring>

namespace bridge {

namespace {

constexpr size_t kMaxFramePayload = 16u * 1024u * 1024u;  // 16 MiB sanity cap

raw_sock_t to_raw(intptr_t h) { return static_cast<raw_sock_t>(h); }

// Read until the HTTP request's blank line ("\r\n\r\n") is seen, then return
// the Sec-WebSocket-Key header value (empty if not found / socket closed).
std::string read_handshake_key(raw_sock_t client) {
    std::string buf;
    char chunk[2048];
    while (buf.find("\r\n\r\n") == std::string::npos) {
#ifdef _WIN32
        const int n = recv(client, chunk, sizeof(chunk), 0);
        if (n == SOCKET_ERROR || n <= 0) return "";
#else
        const ssize_t n = recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) return "";
#endif
        buf.append(chunk, static_cast<size_t>(n));
        if (buf.size() > 64 * 1024) return "";  // not a sane HTTP request
    }

    // Case-insensitive search for the "Sec-WebSocket-Key:" header line.
    std::string lower = buf;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string needle = "sec-websocket-key:";
    const size_t pos = lower.find(needle);
    if (pos == std::string::npos) return "";

    size_t value_start = pos + needle.size();
    while (value_start < buf.size() && (buf[value_start] == ' ' || buf[value_start] == '\t')) ++value_start;
    size_t value_end = buf.find("\r\n", value_start);
    if (value_end == std::string::npos) value_end = buf.size();
    return buf.substr(value_start, value_end - value_start);
}

bool send_raw(raw_sock_t s, const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        const int n = send(s, reinterpret_cast<const char*>(data + sent),
                            static_cast<int>(len - sent), 0);
        if (n == SOCKET_ERROR || n <= 0) return false;
#else
        const ssize_t n = send(s, data + sent, len - sent, 0);
        if (n <= 0) return false;
#endif
        sent += static_cast<size_t>(n);
    }
    return true;
}

} // namespace

WsServer::~WsServer() { stop(); }

bool WsServer::start(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "[ws_server] WSAStartup failed\n");
        return false;
    }
#endif
    const raw_sock_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == kRawInvalid) {
        std::fprintf(stderr, "[ws_server] socket() failed\n");
        return false;
    }

    const int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "[ws_server] bind() on port %u failed\n", port);
        close_sock(server);
        return false;
    }
    listen(server, 1);

    listen_sock_ = static_cast<SocketHandle>(server);
    running_     = true;
    io_thread_   = std::thread(&WsServer::io_thread_main, this, port);
    return true;
}

void WsServer::stop() {
    if (!running_) return;
    running_ = false;

    const raw_sock_t client = to_raw(client_.load());
    if (client != kRawInvalid) close_sock(client);
    client_ = kInvalid;

    const raw_sock_t server = to_raw(listen_sock_);
    if (server != kRawInvalid) close_sock(server);
    listen_sock_ = kInvalid;

    if (io_thread_.joinable()) io_thread_.join();
#ifdef _WIN32
    WSACleanup();
#endif
}

void WsServer::io_thread_main(uint16_t /*port*/) {
    const raw_sock_t server = to_raw(listen_sock_);
    while (running_) {
        sockaddr_in client_addr{};
#ifdef _WIN32
        int addrlen = sizeof(client_addr);
#else
        socklen_t addrlen = sizeof(client_addr);
#endif
        const raw_sock_t client = accept(server, reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
        if (!running_) break;
        if (client == kRawInvalid) continue;

        const std::string key = read_handshake_key(client);
        if (key.empty()) { close_sock(client); continue; }

        const std::string accept_key = compute_accept_key(key);
        const std::string response =
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: " + accept_key + "\r\n"
            "\r\n";
        if (!send_raw(client, reinterpret_cast<const uint8_t*>(response.data()), response.size())) {
            close_sock(client);
            continue;
        }

        std::fprintf(stdout, "[ws_server] client connected\n");
        std::fflush(stdout);
        client_ = static_cast<SocketHandle>(client);

        handle_client(client_.load());

        close_sock(client);
        client_ = kInvalid;
        std::fprintf(stdout, "[ws_server] client disconnected\n");
        std::fflush(stdout);
    }
}

bool WsServer::handle_client(SocketHandle client_handle) {
    const raw_sock_t client = to_raw(client_handle);
    std::string buf;          // raw accumulated bytes not yet parsed into frames
    std::string msg_acc;      // accumulated payload across continuation frames
    uint8_t     msg_opcode = 0;  // 0 = no fragmented message in progress
    char        chunk[4096];

    while (running_) {
#ifdef _WIN32
        const int n = recv(client, chunk, sizeof(chunk), 0);
        if (n == SOCKET_ERROR || n == 0) return false;
#else
        const ssize_t n = recv(client, chunk, sizeof(chunk), 0);
        if (n <= 0) return false;
#endif
        buf.append(chunk, static_cast<size_t>(n));

        // Parse as many complete frames as are currently buffered.
        for (;;) {
            if (buf.size() < 2) break;
            const uint8_t b0 = static_cast<uint8_t>(buf[0]);
            const uint8_t b1 = static_cast<uint8_t>(buf[1]);
            const bool    fin    = (b0 & 0x80) != 0;
            const uint8_t opcode = b0 & 0x0F;
            const bool    masked = (b1 & 0x80) != 0;
            uint64_t      payload_len = b1 & 0x7F;

            size_t header_len = 2;
            if (payload_len == 126) {
                if (buf.size() < 4) break;
                payload_len = (static_cast<uint8_t>(buf[2]) << 8) | static_cast<uint8_t>(buf[3]);
                header_len = 4;
            } else if (payload_len == 127) {
                if (buf.size() < 10) break;
                payload_len = 0;
                for (int i = 0; i < 8; ++i)
                    payload_len = (payload_len << 8) | static_cast<uint8_t>(buf[2 + i]);
                header_len = 10;
            }
            if (payload_len > kMaxFramePayload) return false;  // not a frame we'll honour

            if (masked) header_len += 4;
            const size_t total_needed = header_len + static_cast<size_t>(payload_len);
            if (buf.size() < total_needed) break;  // wait for more bytes

            std::string payload = buf.substr(header_len, static_cast<size_t>(payload_len));
            if (masked) {
                uint8_t mask[4];
                for (int i = 0; i < 4; ++i) mask[i] = static_cast<uint8_t>(buf[header_len - 4 + i]);
                for (size_t i = 0; i < payload.size(); ++i)
                    payload[i] = static_cast<char>(static_cast<uint8_t>(payload[i]) ^ mask[i % 4]);
            }
            buf.erase(0, total_needed);

            switch (opcode) {
                case 0x0:  // continuation
                    msg_acc += payload;
                    if (fin) {
                        if (msg_opcode == 0x1) {
                            std::lock_guard<std::mutex> lk(recv_mutex_);
                            recv_queue_.push(std::move(msg_acc));
                        }
                        msg_acc.clear();
                        msg_opcode = 0;
                    }
                    break;
                case 0x1:  // text
                    if (fin) {
                        std::lock_guard<std::mutex> lk(recv_mutex_);
                        recv_queue_.push(std::move(payload));
                    } else {
                        msg_opcode = 0x1;
                        msg_acc = std::move(payload);
                    }
                    break;
                case 0x2:  // binary — not expected from the GUI; accepted but ignored
                    if (!fin) msg_opcode = 0x2;
                    break;
                case 0x8:  // close
                    send_frame(0x8, nullptr, 0);
                    return true;
                case 0x9:  // ping -> pong
                    send_frame(0xA, reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
                    break;
                case 0xA:  // pong — nothing to do
                    break;
                default:
                    break;  // reserved opcode; ignore
            }
        }
    }
    return true;
}

bool WsServer::send_frame(uint8_t opcode, const uint8_t* data, size_t len) {
    const raw_sock_t client = to_raw(client_.load());
    if (client == kRawInvalid) return false;

    std::string frame;
    frame.push_back(static_cast<char>(0x80 | opcode));  // FIN=1, no fragmentation

    if (len < 126) {
        frame.push_back(static_cast<char>(len));  // MASK bit = 0 (server frames are unmasked)
    } else if (len <= 0xFFFF) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<char>((static_cast<uint64_t>(len) >> (i * 8)) & 0xFF));
    }
    if (len > 0) frame.append(reinterpret_cast<const char*>(data), len);

    std::lock_guard<std::mutex> lk(send_mutex_);
    return send_raw(client, reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
}

bool WsServer::pop_message(std::string& out) {
    std::lock_guard<std::mutex> lk(recv_mutex_);
    if (recv_queue_.empty()) return false;
    out = std::move(recv_queue_.front());
    recv_queue_.pop();
    return true;
}

bool WsServer::send_text(const std::string& payload) {
    return send_frame(0x1, reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
}

bool WsServer::send_binary(const void* data, size_t len) {
    return send_frame(0x2, static_cast<const uint8_t*>(data), len);
}

bool WsServer::is_connected() const {
    return client_.load() != kInvalid;
}

} // namespace bridge
