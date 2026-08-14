/**
 * \file apps/bridge/rigctld_server.cpp
 */

#include "rigctld_server.h"
#include "PAL/logger.h"

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

namespace bridge {

namespace {
raw_sock_t to_raw(intptr_t h) { return static_cast<raw_sock_t>(h); }
} // namespace

RigctldServer::~RigctldServer() { stop(); }

bool RigctldServer::start(uint16_t port, bool bind_remote) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        pal::log_error("rigctld_server", "WSAStartup failed");
        return false;
    }
#endif
    const raw_sock_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == kRawInvalid) {
        pal::log_error("rigctld_server", "socket() failed");
        return false;
    }

    const int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port);
    addr.sin_addr.s_addr = bind_remote ? INADDR_ANY : htonl(INADDR_LOOPBACK);

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        pal::log_error("rigctld_server", "bind() on port %u failed", port);
        close_sock(server);
        return false;
    }
    listen(server, 16);

    bind_remote_ = bind_remote;
    listen_sock_ = static_cast<SocketHandle>(server);
    running_     = true;
    io_thread_   = std::thread(&RigctldServer::io_thread_main, this);
    return true;
}

void RigctldServer::stop() {
    if (!running_) return;
    running_ = false;
    // JOIN FIRST: the I/O thread closes every socket in its cleanup section.
    // Closing sockets here before join would race with any in-flight send.
    if (io_thread_.joinable()) io_thread_.join();
    listen_sock_  = kInvalid;
    client_count_ = 0;
#ifdef _WIN32
    WSACleanup();
#endif
}

// ── Poll-based I/O thread ────────────────────────────────────────────────
//
// Handles the listen socket plus up to kMaxClients persistent client
// connections simultaneously. send_queue_ is drained on every iteration
// (≤5 ms after push from the main thread).

void RigctldServer::io_thread_main() {
    const raw_sock_t listen_raw = to_raw(listen_sock_);

    while (running_) {
        PollFd_t pfds[kMaxClients + 1];
        int      nfds = 0;

        pfds[0].fd      = listen_raw;
        pfds[0].events  = kPollIn;
        pfds[0].revents = 0;
        nfds = 1;

        int slot_pfd[kMaxClients];
        for (int i = 0; i < kMaxClients; ++i) {
            slot_pfd[i] = -1;
            if (slots_[i].fd != kInvalid) {
                pfds[nfds].fd      = to_raw(slots_[i].fd);
                pfds[nfds].events  = kPollIn;
                pfds[nfds].revents = 0;
                slot_pfd[i] = nfds++;
            }
        }

        do_poll(pfds, nfds, 5);  // 5 ms timeout — guarantees ≤5 ms shutdown latency
        if (!running_) break;

        // ① Accept a new connection ─────────────────────────────────────
        if (pfds[0].revents & kPollIn) {
            sockaddr_in caddr{};
#ifdef _WIN32
            int addrlen = sizeof(caddr);
#else
            socklen_t addrlen = sizeof(caddr);
#endif
            const raw_sock_t fd = accept(listen_raw, reinterpret_cast<sockaddr*>(&caddr), &addrlen);
            if (fd != kRawInvalid) {
                bool placed = false;
                for (int i = 0; i < kMaxClients; ++i) {
                    if (slots_[i].fd == kInvalid) {
                        slots_[i].fd = static_cast<SocketHandle>(fd);
                        slots_[i].recv_buf.clear();
                        slots_[i].id = next_conn_id_.fetch_add(1);
                        client_count_.fetch_add(1);
                        placed = true;
                        pal::log_info("rigctld_server", "client connected (id=%llu, total=%zu)",
                                      static_cast<unsigned long long>(slots_[i].id),
                                      client_count_.load());
                        break;
                    }
                }
                if (!placed) close_sock(fd);  // all kMaxClients slots full
            }
        }

        // ② Service active clients — recv + line-split ────────────────────
        for (int i = 0; i < kMaxClients; ++i) {
            if (slot_pfd[i] < 0 || slots_[i].fd == kInvalid) continue;

            const short rev = pfds[slot_pfd[i]].revents;
            bool close_conn = (rev & (kPollHup | kPollErr)) != 0;

            if (!close_conn && (rev & kPollIn)) {
                char chunk[4096];
                const raw_sock_t fd = to_raw(slots_[i].fd);
#ifdef _WIN32
                const int n = recv(fd, chunk, sizeof(chunk), 0);
#else
                const ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
#endif
                if (n <= 0) {
                    close_conn = true;
                } else {
                    slots_[i].recv_buf.append(chunk, static_cast<size_t>(n));
                    // Sanity cap: a misbehaving client sending an unterminated
                    // multi-MB line would otherwise grow this buffer unbounded.
                    if (slots_[i].recv_buf.size() > 64 * 1024) {
                        close_conn = true;
                    } else {
                        size_t pos;
                        while ((pos = slots_[i].recv_buf.find('\n')) != std::string::npos) {
                            std::string line = slots_[i].recv_buf.substr(0, pos);
                            slots_[i].recv_buf.erase(0, pos + 1);
                            if (!line.empty() && line.back() == '\r') line.pop_back();
                            std::lock_guard<std::mutex> lk(recv_mtx_);
                            recv_queue_.emplace(slots_[i].id, std::move(line));
                        }
                    }
                }
            }

            if (close_conn) {
                close_sock(to_raw(slots_[i].fd));
                pal::log_info("rigctld_server", "client disconnected (id=%llu)",
                              static_cast<unsigned long long>(slots_[i].id));
                slots_[i].fd = kInvalid;
                slots_[i].recv_buf.clear();
                slots_[i].id = 0;
                client_count_.fetch_sub(1);
            }
        }

        // ③ Drain send_queue_ → matching client ────────────────────────────
        std::queue<std::pair<uint64_t, std::string>> local;
        {
            std::lock_guard<std::mutex> lk(send_mtx_);
            std::swap(local, send_queue_);
        }
        while (!local.empty()) {
            const uint64_t     conn_id = local.front().first;
            const std::string& text    = local.front().second;
            for (int i = 0; i < kMaxClients; ++i) {
                if (slots_[i].fd == kInvalid || slots_[i].id != conn_id) continue;
                const raw_sock_t fd = to_raw(slots_[i].fd);
                size_t sent = 0;
                while (sent < text.size()) {
#ifdef _WIN32
                    const int n = send(fd, text.data() + sent,
                                        static_cast<int>(text.size() - sent), 0);
                    if (n == SOCKET_ERROR || n <= 0) break;
#else
                    const ssize_t n = send(fd, text.data() + sent, text.size() - sent, 0);
                    if (n <= 0) break;
#endif
                    sent += static_cast<size_t>(n);
                }
                break;  // conn_id is unique among active slots
            }
            local.pop();
        }
    }

    // ── Cleanup (running_ == false, called once) ───────────────────────────
    for (int i = 0; i < kMaxClients; ++i) {
        if (slots_[i].fd != kInvalid) {
            close_sock(to_raw(slots_[i].fd));
            slots_[i].fd = kInvalid;
        }
    }
    close_sock(listen_raw);
    listen_sock_ = kInvalid;
}

// ── Public API ───────────────────────────────────────────────────────────

bool RigctldServer::pop_request(uint64_t& conn_id, std::string& line) {
    std::lock_guard<std::mutex> lk(recv_mtx_);
    if (recv_queue_.empty()) return false;
    conn_id = recv_queue_.front().first;
    line    = std::move(recv_queue_.front().second);
    recv_queue_.pop();
    return true;
}

bool RigctldServer::send_reply(uint64_t conn_id, const std::string& text) {
    if (!running_) return false;
    std::lock_guard<std::mutex> lk(send_mtx_);
    send_queue_.emplace(conn_id, text);
    return true;
}

} // namespace bridge
