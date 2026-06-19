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
   // Poll abstraction: WSAPoll uses WSAPOLLFD + POLLRDNORM for readability
   using PollFd_t = WSAPOLLFD;
   static constexpr short kPollIn  = POLLRDNORM;
   static constexpr short kPollHup = POLLHUP;
   static constexpr short kPollErr = POLLERR;
   static int do_poll(PollFd_t* fds, int n, int ms) {
       return WSAPoll(fds, static_cast<ULONG>(n), ms);
   }
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <poll.h>
   using raw_sock_t = int;
   static constexpr raw_sock_t kRawInvalid = -1;
   static void close_sock(raw_sock_t s) { close(s); }
   using PollFd_t = struct pollfd;
   static constexpr short kPollIn  = POLLIN;
   static constexpr short kPollHup = POLLHUP;
   static constexpr short kPollErr = POLLERR;
   static int do_poll(PollFd_t* fds, int n, int ms) {
       return poll(fds, static_cast<nfds_t>(n), ms);
   }
#endif

#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace bridge {

namespace {

constexpr size_t kMaxFramePayload = 16u * 1024u * 1024u;  // 16 MiB sanity cap

raw_sock_t to_raw(intptr_t h) { return static_cast<raw_sock_t>(h); }

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string extract_ws_key(const std::string& req) {
    const std::string lower = to_lower(req);
    const std::string needle = "sec-websocket-key:";
    const size_t pos = lower.find(needle);
    if (pos == std::string::npos) return "";
    size_t value_start = pos + needle.size();
    while (value_start < req.size() && (req[value_start] == ' ' || req[value_start] == '\t')) ++value_start;
    size_t value_end = req.find("\r\n", value_start);
    if (value_end == std::string::npos) value_end = req.size();
    return req.substr(value_start, value_end - value_start);
}

bool is_ws_upgrade(const std::string& req) {
    const std::string lower = to_lower(req);
    return lower.find("upgrade: websocket") != std::string::npos
        || lower.find("sec-websocket-key:") != std::string::npos;
}

std::string request_path(const std::string& req) {
    const size_t sp1 = req.find(' ');
    if (sp1 == std::string::npos) return "/";
    const size_t start = sp1 + 1;
    const size_t sp2 = req.find(' ', start);
    if (sp2 == std::string::npos) return "/";
    std::string path = req.substr(start, sp2 - start);
    const size_t q = path.find('?');
    if (q != std::string::npos) path.erase(q);
    return path.empty() ? "/" : path;
}

const char* mime_for(const std::string& path) {
    auto ends_with = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (ends_with(".html") || ends_with(".htm")) return "text/html; charset=utf-8";
    if (ends_with(".js"))                        return "application/javascript; charset=utf-8";
    if (ends_with(".css"))                       return "text/css; charset=utf-8";
    if (ends_with(".json"))                      return "application/json; charset=utf-8";
    if (ends_with(".svg"))                       return "image/svg+xml";
    if (ends_with(".png"))                       return "image/png";
    if (ends_with(".ico"))                       return "image/x-icon";
    return "application/octet-stream";
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

void send_http(raw_sock_t client, const char* status, const char* content_type,
               const std::string& body) {
    std::string resp = std::string("HTTP/1.1 ") + status + "\r\n";
    resp += std::string("Content-Type: ") + content_type + "\r\n";
    resp += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    resp += "Connection: close\r\n\r\n";
    resp += body;
    send_raw(client, reinterpret_cast<const uint8_t*>(resp.data()), resp.size());
}

void serve_static(raw_sock_t client, const std::string& req, const std::string& web_root) {
    std::string path = request_path(req);
    if (path == "/") path = "/index.html";
    if (path.find("..") != std::string::npos || web_root.empty()) {
        send_http(client, "404 Not Found", "text/plain", "Not Found");
        return;
    }
    const std::string full = web_root + path;
    std::ifstream f(full, std::ios::binary);
    if (!f) {
        send_http(client, "404 Not Found", "text/plain", "Not Found");
        return;
    }
    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    send_http(client, "200 OK", mime_for(path), body);
}

// Build and send one WS frame (unmasked, FIN=1).  I/O-thread only — no mutex.
bool ioth_send_frame(raw_sock_t s, uint8_t opcode, const uint8_t* data, size_t len) {
    std::string frame;
    frame.push_back(static_cast<char>(0x80 | opcode));
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
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
    return send_raw(s, reinterpret_cast<const uint8_t*>(frame.data()), frame.size());
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

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
    listen(server, 16);

    listen_sock_ = static_cast<SocketHandle>(server);
    running_     = true;
    io_thread_   = std::thread(&WsServer::io_thread_main, this, port);
    return true;
}

void WsServer::stop() {
    if (!running_) return;
    running_ = false;
    // JOIN FIRST: the I/O thread closes all sockets in its cleanup section.
    // Closing sockets here before join would race with any in-flight send.
    if (io_thread_.joinable()) io_thread_.join();
    listen_sock_ = kInvalid;
    client_      = kInvalid;
#ifdef _WIN32
    WSACleanup();
#endif
}

// ── Poll-based I/O thread ────────────────────────────────────────────────────
//
// Handles three kinds of fds simultaneously:
//   listen_sock_      — new incoming connections (HTTP or WS upgrade)
//   pending_http_[]   — HTTP requests not yet completely received/served
//   client_           — active WebSocket session
//
// send_queue_ is drained on every iteration (≤5 ms after push from main thread).

void WsServer::io_thread_main(uint16_t /*port*/) {
    const raw_sock_t listen_raw = to_raw(listen_sock_);

    while (running_) {
        // ── Build poll set ────────────────────────────────────────────────
        PollFd_t pfds[kMaxPendingHttp + 2];
        int      nfds = 0;

        // Slot 0: listen socket
        pfds[0].fd      = listen_raw;
        pfds[0].events  = kPollIn;
        pfds[0].revents = 0;
        nfds = 1;

        // Slot 1 (optional): active WS client
        int ws_slot = -1;
        const raw_sock_t ws_raw = to_raw(client_.load());
        if (ws_raw != kRawInvalid) {
            pfds[nfds].fd      = ws_raw;
            pfds[nfds].events  = kPollIn;
            pfds[nfds].revents = 0;
            ws_slot = nfds++;
        }

        // Remaining slots: pending HTTP connections
        int http_slots[kMaxPendingHttp];
        for (int i = 0; i < kMaxPendingHttp; ++i) {
            http_slots[i] = -1;
            if (pending_http_[i].fd != kInvalid) {
                pfds[nfds].fd      = to_raw(pending_http_[i].fd);
                pfds[nfds].events  = kPollIn;
                pfds[nfds].revents = 0;
                http_slots[i] = nfds++;
            }
        }

        do_poll(pfds, nfds, 5);  // 5 ms timeout — guarantees ≤5 ms shutdown latency
        if (!running_) break;

        // ① Accept new connections ─────────────────────────────────────────
        if (pfds[0].revents & kPollIn) {
            sockaddr_in client_addr{};
#ifdef _WIN32
            int addrlen = sizeof(client_addr);
#else
            socklen_t addrlen = sizeof(client_addr);
#endif
            const raw_sock_t fd = accept(listen_raw,
                                         reinterpret_cast<sockaddr*>(&client_addr), &addrlen);
            if (fd != kRawInvalid) {
                bool placed = false;
                for (int i = 0; i < kMaxPendingHttp; ++i) {
                    if (pending_http_[i].fd == kInvalid) {
                        pending_http_[i].fd = static_cast<SocketHandle>(fd);
                        pending_http_[i].buf.clear();
                        placed = true;
                        break;
                    }
                }
                if (!placed) close_sock(fd);  // all slots full (>8 parallel connections)
            }
        }

        // ② Serve pending HTTP connections ────────────────────────────────
        for (int i = 0; i < kMaxPendingHttp; ++i) {
            if (http_slots[i] < 0 || pending_http_[i].fd == kInvalid) continue;

            const short rev = pfds[http_slots[i]].revents;
            if (rev & (kPollHup | kPollErr)) {
                close_sock(to_raw(pending_http_[i].fd));
                pending_http_[i].fd = kInvalid;
                pending_http_[i].buf.clear();
                continue;
            }
            if (!(rev & kPollIn)) continue;

            char chunk[2048];
#ifdef _WIN32
            const int n = recv(to_raw(pending_http_[i].fd), chunk, sizeof(chunk), 0);
            const bool recv_ok = (n > 0);
            const size_t nbytes = recv_ok ? static_cast<size_t>(n) : 0;
#else
            const ssize_t n = recv(to_raw(pending_http_[i].fd), chunk, sizeof(chunk), 0);
            const bool recv_ok = (n > 0);
            const size_t nbytes = recv_ok ? static_cast<size_t>(n) : 0;
#endif
            if (!recv_ok) {
                close_sock(to_raw(pending_http_[i].fd));
                pending_http_[i].fd = kInvalid;
                pending_http_[i].buf.clear();
                continue;
            }
            pending_http_[i].buf.append(chunk, nbytes);

            // Oversized request — abort
            if (pending_http_[i].buf.size() > 64 * 1024) {
                close_sock(to_raw(pending_http_[i].fd));
                pending_http_[i].fd = kInvalid;
                pending_http_[i].buf.clear();
                continue;
            }

            // Wait for complete HTTP request head ("\r\n\r\n")
            if (pending_http_[i].buf.find("\r\n\r\n") == std::string::npos) continue;

            const std::string& req = pending_http_[i].buf;
            const raw_sock_t   fd  = to_raw(pending_http_[i].fd);

            if (is_ws_upgrade(req)) {
                const std::string key = extract_ws_key(req);
                if (!key.empty() && client_.load() == kInvalid) {
                    // Promote connection to WebSocket session
                    const std::string accept_key = compute_accept_key(key);
                    const std::string response =
                        "HTTP/1.1 101 Switching Protocols\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: Upgrade\r\n"
                        "Sec-WebSocket-Accept: " + accept_key + "\r\n"
                        "\r\n";
                    send_raw(fd, reinterpret_cast<const uint8_t*>(response.data()), response.size());
                    ws_recv_buf_.clear();
                    ws_frag_acc_.clear();
                    ws_frag_opcode_ = 0;
                    client_ = static_cast<SocketHandle>(fd);
                    std::fprintf(stdout, "[ws_server] client connected\n");
                    std::fflush(stdout);
                } else {
                    // A WS session is already active, or the Sec-WebSocket-Key header is
                    // missing/malformed.  Respond 503 (not 400: the request is syntactically
                    // valid; the server is simply single-session by design) and close the
                    // socket immediately.  No state is changed.
                    const std::string reject =
                        "HTTP/1.1 503 Service Unavailable\r\n"
                        "Content-Length: 0\r\n"
                        "Connection: close\r\n\r\n";
                    send_raw(fd, reinterpret_cast<const uint8_t*>(reject.data()), reject.size());
                    close_sock(fd);
                    std::fprintf(stdout, "[ws_server] WS upgrade rejected — session already active\n");
                    std::fflush(stdout);
                }
            } else {
                serve_static(fd, req, web_root_);
                close_sock(fd);
            }

            pending_http_[i].fd = kInvalid;
            pending_http_[i].buf.clear();
        }

        // ③ WebSocket recv ─────────────────────────────────────────────────
        if (ws_slot >= 0) {
            const short rev = pfds[ws_slot].revents;
            bool close_conn = (rev & (kPollHup | kPollErr)) != 0;

            if (!close_conn && (rev & kPollIn)) {
                char chunk[4096];
                const raw_sock_t wfd = to_raw(client_.load());
#ifdef _WIN32
                const int rn = recv(wfd, chunk, sizeof(chunk), 0);
                if (rn <= 0) close_conn = true;
#else
                const ssize_t rn = recv(wfd, chunk, sizeof(chunk), 0);
                if (rn <= 0) close_conn = true;
#endif
                else {
                    ws_recv_buf_.append(chunk, static_cast<size_t>(rn));
                    close_conn = parse_ws_frames_(client_.load());
                }
            }

            if (close_conn) {
                close_sock(to_raw(client_.load()));
                client_ = kInvalid;
                ws_recv_buf_.clear();
                ws_frag_acc_.clear();
                ws_frag_opcode_ = 0;
                std::fprintf(stdout, "[ws_server] client disconnected\n");
                std::fflush(stdout);
            }
        }

        // ④ Drain send_queue → WS client ──────────────────────────────────
        if (client_.load() != kInvalid) {
            std::queue<SendItem> local;
            {
                std::lock_guard<std::mutex> lk(send_queue_mtx_);
                std::swap(local, send_queue_);
            }
            const raw_sock_t wfd = to_raw(client_.load());
            if (wfd != kRawInvalid) {
                while (!local.empty()) {
                    const SendItem& item = local.front();
                    ioth_send_frame(wfd, item.opcode, item.data.data(), item.data.size());
                    local.pop();
                }
            }
        }
    }

    // ── Cleanup (running_ == false, called once) ──────────────────────────
    for (int i = 0; i < kMaxPendingHttp; ++i) {
        if (pending_http_[i].fd != kInvalid) {
            close_sock(to_raw(pending_http_[i].fd));
            pending_http_[i].fd = kInvalid;
        }
    }
    {
        const raw_sock_t wfd = to_raw(client_.load());
        if (wfd != kRawInvalid) {
            close_sock(wfd);
            client_ = kInvalid;
        }
    }
    close_sock(listen_raw);
    listen_sock_ = kInvalid;
}

// ── WS frame parser (I/O thread only) ───────────────────────────────────────
//
// Parses as many complete frames as are available in ws_recv_buf_.  Sends
// pong/close responses inline.  Returns true if a Close frame was received
// (caller should then close and clear the WS session).

bool WsServer::parse_ws_frames_(SocketHandle ws_handle) {
    const raw_sock_t ws_sock = to_raw(ws_handle);
    std::string&     buf     = ws_recv_buf_;

    for (;;) {
        if (buf.size() < 2) break;
        const uint8_t b0 = static_cast<uint8_t>(buf[0]);
        const uint8_t b1 = static_cast<uint8_t>(buf[1]);
        const bool    fin     = (b0 & 0x80) != 0;
        const uint8_t opcode  = b0 & 0x0F;
        const bool    masked  = (b1 & 0x80) != 0;
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
        if (payload_len > kMaxFramePayload) return true;  // protocol violation → close

        if (masked) header_len += 4;
        const size_t total_needed = header_len + static_cast<size_t>(payload_len);
        if (buf.size() < total_needed) break;

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
                ws_frag_acc_ += payload;
                if (fin) {
                    if (ws_frag_opcode_ == 0x1) {
                        std::lock_guard<std::mutex> lk(recv_mutex_);
                        recv_queue_.push(std::move(ws_frag_acc_));
                    }
                    ws_frag_acc_.clear();
                    ws_frag_opcode_ = 0;
                }
                break;
            case 0x1:  // text
                if (fin) {
                    std::lock_guard<std::mutex> lk(recv_mutex_);
                    recv_queue_.push(std::move(payload));
                } else {
                    ws_frag_opcode_ = 0x1;
                    ws_frag_acc_ = std::move(payload);
                }
                break;
            case 0x2:  // binary — not expected from the GUI; accepted but ignored
                if (!fin) ws_frag_opcode_ = 0x2;
                break;
            case 0x8:  // close → echo close frame and signal disconnect
                ioth_send_frame(ws_sock, 0x8, nullptr, 0);
                return true;
            case 0x9:  // ping → pong
                ioth_send_frame(ws_sock, 0xA,
                    reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
                break;
            case 0xA:  // pong — nothing to do
                break;
            default:
                break;
        }
    }
    return false;
}

// ── Public API ───────────────────────────────────────────────────────────────

bool WsServer::pop_message(std::string& out) {
    std::lock_guard<std::mutex> lk(recv_mutex_);
    if (recv_queue_.empty()) return false;
    out = std::move(recv_queue_.front());
    recv_queue_.pop();
    return true;
}

bool WsServer::send_text(const std::string& payload) {
    if (!is_connected()) return false;
    SendItem item;
    item.opcode = 0x1;
    item.data.assign(payload.begin(), payload.end());
    std::lock_guard<std::mutex> lk(send_queue_mtx_);
    if (send_queue_.size() >= kSendQueueLimit) send_queue_.pop();  // drop oldest
    send_queue_.push(std::move(item));
    return true;
}

bool WsServer::send_binary(const void* data, size_t len) {
    if (!is_connected()) return false;
    SendItem item;
    item.opcode = 0x2;
    const auto* p = static_cast<const uint8_t*>(data);
    item.data.assign(p, p + len);
    std::lock_guard<std::mutex> lk(send_queue_mtx_);
    if (send_queue_.size() >= kSendQueueLimit) send_queue_.pop();  // drop oldest
    send_queue_.push(std::move(item));
    return true;
}

bool WsServer::is_connected() const {
    return client_.load() != kInvalid;
}

} // namespace bridge
