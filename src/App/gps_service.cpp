#include "App/gps_service.h"

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

// ── Platform socket abstraction (mirrors apps/bridge/ws_server.cpp) ──────────
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using raw_sock_t = SOCKET;
   static constexpr raw_sock_t kInvalidSock = INVALID_SOCKET;
   static void sock_close(raw_sock_t s) { closesocket(s); }
   static int  sock_recv(raw_sock_t s, char* buf, int n) {
       return static_cast<int>(recv(s, buf, n, 0));
   }
   static bool sock_init() {
       WSADATA wd; return WSAStartup(MAKEWORD(2,2), &wd) == 0;
   }
#else
#  include <sys/socket.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <fcntl.h>
   using raw_sock_t = int;
   static constexpr raw_sock_t kInvalidSock = -1;
   static void sock_close(raw_sock_t s) { ::close(s); }
   static int  sock_recv(raw_sock_t s, char* buf, int n) {
       return static_cast<int>(::recv(s, buf, n, 0));
   }
   static bool sock_init() { return true; }
#endif

// ── Serial port abstraction ───────────────────────────────────────────────────
#ifdef _WIN32
#  include <windows.h>
   using serial_t = HANDLE;
   static const serial_t kInvalidSerial = INVALID_HANDLE_VALUE;
   static serial_t serial_open(const std::string& port, uint32_t baud) {
       std::string p = "\\\\.\\" + port;
       serial_t h = CreateFileA(p.c_str(), GENERIC_READ, 0, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
       if (h == INVALID_HANDLE_VALUE) return h;
       DCB dcb{};  dcb.DCBlength = sizeof(dcb);
       GetCommState(h, &dcb);
       dcb.BaudRate = static_cast<DWORD>(baud);
       dcb.ByteSize = 8;  dcb.StopBits = ONESTOPBIT;  dcb.Parity = NOPARITY;
       SetCommState(h, &dcb);
       COMMTIMEOUTS ct{};
       ct.ReadIntervalTimeout        = 500;
       ct.ReadTotalTimeoutMultiplier = 0;
       ct.ReadTotalTimeoutConstant   = 1000;
       SetCommTimeouts(h, &ct);
       return h;
   }
   static void serial_close(serial_t h) { CloseHandle(h); }
   static int  serial_read(serial_t h, char* buf, int n) {
       DWORD got = 0;
       return ReadFile(h, buf, static_cast<DWORD>(n), &got, nullptr) ? static_cast<int>(got) : -1;
   }
#else
#  include <termios.h>
   using serial_t = int;
   static constexpr serial_t kInvalidSerial = -1;
   static speed_t baud_to_speed(uint32_t b) {
       switch (b) {
           case 4800:   return B4800;
           case 9600:   return B9600;
           case 19200:  return B19200;
           case 38400:  return B38400;
           case 57600:  return B57600;
           case 115200: return B115200;
           default:     return B4800;
       }
   }
   static serial_t serial_open(const std::string& port, uint32_t baud) {
       int fd = ::open(port.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
       if (fd < 0) return kInvalidSerial;
       struct termios t{};
       tcgetattr(fd, &t);
       cfsetispeed(&t, baud_to_speed(baud));
       t.c_cflag = CS8 | CLOCAL | CREAD;
       t.c_iflag = IGNPAR;
       t.c_oflag = 0;
       t.c_lflag = 0;
       t.c_cc[VMIN]  = 0;
       t.c_cc[VTIME] = 10;  // 1 s read timeout
       tcsetattr(fd, TCSANOW, &t);
       // Switch back to blocking with timeout
       int flags = fcntl(fd, F_GETFL, 0);
       fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
       return fd;
   }
   static void serial_close(serial_t fd) { ::close(fd); }
   static int  serial_read(serial_t fd, char* buf, int n) {
       return static_cast<int>(::read(fd, buf, static_cast<size_t>(n)));
   }
#endif

namespace ale {

// ── TCP connect helper ────────────────────────────────────────────────────────

static raw_sock_t tcp_connect(const std::string& host, uint16_t port) {
    sock_init();
    struct addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    const std::string port_str = std::to_string(port);
    if (getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res) != 0) return kInvalidSock;
    raw_sock_t s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == kInvalidSock) { freeaddrinfo(res); return kInvalidSock; }
    if (connect(s, res->ai_addr, static_cast<int>(res->ai_addrlen)) != 0) {
        sock_close(s); freeaddrinfo(res); return kInvalidSock;
    }
    freeaddrinfo(res);
    return s;
}

// ── GpsService ────────────────────────────────────────────────────────────────

void GpsService::start(const Config& cfg, FixCallback on_fix_change) {
    if (running_.load()) stop();
    {
        std::lock_guard<std::mutex> g(cb_mtx_);
        on_fix_ = std::move(on_fix_change);
    }
    running_ = true;
    if (cfg.gpsd_enabled)
        gpsd_thread_ = std::thread(&GpsService::gpsd_loop, this, cfg);
    if (cfg.nmea_enabled)
        nmea_thread_ = std::thread(&GpsService::nmea_loop, this, cfg);
}

void GpsService::stop() {
    running_ = false;
    if (gpsd_thread_.joinable()) gpsd_thread_.join();
    if (nmea_thread_.joinable()) nmea_thread_.join();
    // Reset fix state on stop
    std::lock_guard<std::mutex> g(fix_mtx_);
    fix_valid_   = false;
    fix_has_alt_ = false;
}

bool GpsService::has_fix() const {
    std::lock_guard<std::mutex> g(fix_mtx_);
    return fix_valid_;
}
double GpsService::lat() const {
    std::lock_guard<std::mutex> g(fix_mtx_);
    return fix_lat_;
}
double GpsService::lon() const {
    std::lock_guard<std::mutex> g(fix_mtx_);
    return fix_lon_;
}

bool GpsService::has_altitude() const {
    std::lock_guard<std::mutex> g(fix_mtx_);
    return fix_valid_ && fix_has_alt_;
}
double GpsService::alt() const {
    std::lock_guard<std::mutex> g(fix_mtx_);
    return fix_alt_;
}
std::string GpsService::raw_gga() const {
    std::lock_guard<std::mutex> g(fix_mtx_);
    return raw_gga_;
}

void GpsService::update_fix(bool valid, double lat, double lon, bool has_alt, double alt) {
    bool changed;
    {
        std::lock_guard<std::mutex> g(fix_mtx_);
        changed = (valid != fix_valid_) ||
                  (valid && (lat != fix_lat_ || lon != fix_lon_));
        fix_valid_   = valid;
        fix_has_alt_ = valid && has_alt;
        if (valid) { fix_lat_ = lat; fix_lon_ = lon; fix_alt_ = has_alt ? alt : 0.0; }
    }
    if (changed) {
        std::lock_guard<std::mutex> g(cb_mtx_);
        if (on_fix_) on_fix_(valid, lat, lon);
    }
}

// ── gpsd loop ─────────────────────────────────────────────────────────────────

void GpsService::gpsd_loop(Config cfg) {
    while (running_.load()) {
        raw_sock_t s = tcp_connect(cfg.gpsd_host, cfg.gpsd_port);
        if (s == kInvalidSock) {
            for (int i = 0; i < 50 && running_.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Enable JSON streaming
        const char* watch = "?WATCH={\"enable\":true,\"json\":true}\n";
        send(s, watch, static_cast<int>(strlen(watch)), 0);

        std::string linebuf;
        char chunk[512];
        while (running_.load()) {
            int n = sock_recv(s, chunk, static_cast<int>(sizeof(chunk) - 1));
            if (n <= 0) break;
            chunk[n] = '\0';
            linebuf += chunk;
            size_t pos;
            while ((pos = linebuf.find('\n')) != std::string::npos) {
                std::string line = linebuf.substr(0, pos);
                linebuf.erase(0, pos + 1);
                if (line.find("\"class\":\"TPV\"") != std::string::npos) {
                    double lat, lon; bool fix_ok;
                    bool has_alt; double alt;
                    if (parse_tpv_json(line, lat, lon, fix_ok, has_alt, alt))
                        update_fix(fix_ok, lat, lon, has_alt, alt);
                }
            }
        }
        sock_close(s);
        update_fix(false, 0.0, 0.0);
    }
}

// ── NMEA loop ─────────────────────────────────────────────────────────────────

void GpsService::nmea_loop(Config cfg) {
    while (running_.load()) {
        serial_t fd = serial_open(cfg.nmea_port, cfg.nmea_baud);
        if (fd == kInvalidSerial) {
            for (int i = 0; i < 50 && running_.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::string linebuf;
        char ch;
        while (running_.load()) {
            int n = serial_read(fd, &ch, 1);
            if (n < 0) break;
            if (n == 0) continue;
            if (ch == '\n') {
                // Strip trailing \r
                if (!linebuf.empty() && linebuf.back() == '\r') linebuf.pop_back();
                double lat, lon; bool active;
                if (linebuf.size() > 6) {
                    const std::string prefix = linebuf.substr(0, 6);
                    if (prefix == "$GPGGA" || prefix == "$GNGGA") {
                        bool has_alt; double alt;
                        if (parse_gpgga(linebuf, lat, lon, has_alt, alt)) {
                            { std::lock_guard<std::mutex> g(fix_mtx_); raw_gga_ = linebuf; }
                            update_fix(true, lat, lon, has_alt, alt);
                        }
                    } else if (prefix == "$GPRMC" || prefix == "$GNRMC") {
                        if (parse_gprmc(linebuf, lat, lon, active))
                            update_fix(active, lat, lon);
                        else
                            update_fix(false, 0.0, 0.0);
                    }
                }
                linebuf.clear();
            } else {
                linebuf += ch;
            }
        }
        serial_close(fd);
        update_fix(false, 0.0, 0.0);
    }
}

// ── Static parsers ─────────────────────────────────────────────────────────────

bool GpsService::parse_tpv_json(const std::string& json,
                                 double& lat, double& lon, bool& fix_ok,
                                 bool& has_alt, double& alt)
{
    // Hand-rolled token search: no external JSON dep.
    auto find_num = [&](const std::string& key, bool& found) -> double {
        const size_t p = json.find("\"" + key + "\":");
        if (p == std::string::npos) { found = false; return 0.0; }
        size_t vs = json.find_first_of("-0123456789", p + key.size() + 3);
        if (vs == std::string::npos) { found = false; return 0.0; }
        found = true;
        return std::stod(json.substr(vs));
    };

    // "mode": 2 = 2D fix, 3 = 3D fix; 0/1 = no fix
    const size_t mp = json.find("\"mode\":");
    if (mp == std::string::npos) return false;
    const size_t vs = json.find_first_of("0123456789", mp + 7);
    if (vs == std::string::npos) return false;
    const int mode = std::stoi(json.substr(vs));
    fix_ok = (mode >= 2);

    bool found;
    lat = find_num("lat", found);
    lon = find_num("lon", found);
    alt = find_num("alt", has_alt);  // gpsd TPV "alt" (MSL altitude, meters)
    return true;
}

double GpsService::nmea_coord(const std::string& coord, const std::string& hemi) {
    // NMEA format: DDDMM.MMMMM  or DDMM.MMMMM
    if (coord.size() < 5) return 0.0;
    const size_t dot = coord.find('.');
    if (dot == std::string::npos || dot < 2) return 0.0;
    const int deg_digits = static_cast<int>(dot) - 2;  // 2 = lon 3-digit prefix on dddmm
    const double deg = std::stod(coord.substr(0, deg_digits));
    const double min = std::stod(coord.substr(deg_digits));
    double result = deg + min / 60.0;
    if (hemi == "S" || hemi == "W") result = -result;
    return result;
}

static std::vector<std::string> nmea_split(const std::string& s) {
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ',' || s[i] == '*') {
            fields.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return fields;
}

bool GpsService::parse_gpgga(const std::string& s, double& lat, double& lon,
                              bool& has_alt, double& alt) {
    // $GPGGA,time,lat,N,lon,E,quality,numSV,HDOP,alt,altUnit,...
    auto f = nmea_split(s);
    if (f.size() < 7) return false;
    if (f[6].empty() || f[6] == "0") return false;  // quality 0 = no fix
    if (f[2].empty() || f[4].empty()) return false;
    lat = nmea_coord(f[2], f[3]);
    lon = nmea_coord(f[4], f[5]);
    has_alt = (f.size() > 9 && !f[9].empty());
    alt = has_alt ? std::stod(f[9]) : 0.0;  // field 9 = MSL altitude, meters
    return true;
}

bool GpsService::parse_gprmc(const std::string& s, double& lat, double& lon, bool& active) {
    // $GPRMC,time,A/V,lat,N,lon,E,...
    auto f = nmea_split(s);
    if (f.size() < 7) return false;
    active = (!f[2].empty() && f[2][0] == 'A');
    if (!active || f[3].empty() || f[5].empty()) return false;
    lat = nmea_coord(f[3], f[4]);
    lon = nmea_coord(f[5], f[6]);
    return true;
}

} // namespace ale
