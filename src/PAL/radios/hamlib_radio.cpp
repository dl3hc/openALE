// hamlib_radio.cpp

#ifdef _MSC_VER
#pragma warning(disable: 4996)  // strncpy: safe usage with explicit null termination below
#endif

#include "PAL/radios/hamlib_radio.h"
#include "PAL/logger.h"
#include <hamlib/rig.h>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace pal {

static rmode_t        to_hamlib_mode(RadioMode mode);
static RadioMode      from_hamlib_mode(rmode_t m);

// Maps the GUI log-level integer (0=Off 1=Error 2=Info 3=Debug 4=Trace) to
// the PAL logger level and forwards it. Info shows channel/freq transitions
// + assert_mode readback detail; Debug adds sync detail.
void hamlib_set_log_level(int level) {
    auto* logger = pal::get_logger();
    if (!logger) return;
    static const pal::LogLevel kMap[] = {
        static_cast<pal::LogLevel>(5),  // 0 = Off   → FATAL
        static_cast<pal::LogLevel>(4),  // 1 = Error  → ERROR
        static_cast<pal::LogLevel>(2),  // 2 = Info   → INFO
        static_cast<pal::LogLevel>(1),  // 3 = Debug  → DEBUG
        static_cast<pal::LogLevel>(0),  // 4 = Trace  → TRACE
    };
    const int idx = (level >= 0 && level <= 4) ? level : 2;
    logger->set_level(kMap[idx]);
}

// sync_from_radio() re-asserts the intended mode only within this window
// after the last mode command. An SDR front-end (Quisk) applies its
// band-memory mode restore asynchronously, possibly AFTER assert_mode()'s
// readback loop returned — silently reverting a one-shot channel activation
// (manual step/net select). The 2 s VFO_GET poll is the only later observer
// so it must correct the revert; the window scopes that correction to the
// aftermath of our own command — a mode the operator changes minutes later is NOT fought.
static constexpr auto MODE_REASSERT_WINDOW = std::chrono::seconds(5);

HamlibRadio::HamlibRadio(const std::string& model,
                         const std::string& port,
                         int baud,
                         SerialLinePolicy policy,
                         PttPolicy ptt_policy)
    : model_(model),
      port_(port),
      baud_(baud),
      policy_(policy),
      ptt_policy_(ptt_policy) {}

HamlibRadio::~HamlibRadio() {
    shutdown();
}

bool HamlibRadio::initialize() {
    if (ready_.load()) return true;

    rig_set_debug(RIG_DEBUG_ERR);

    // Ensures all statically-linked backends are registered; usually
    // redundant with a monolithic libhamlib-4.dll but harmless.
    rig_load_all_backends();

    rig_ = rig_init(std::stoi(model_));
    if (!rig_) {
        pal::log_error("HamlibRadio", "rig_init(%s) failed", model_.c_str());
        return false;
    }

    if (!configure_port()) {
        shutdown();
        return false;
    }

    // ready_ stays false until start() calls rig_open() successfully.
    pal::log_info("HamlibRadio", "rig_init(%s) -> rig_open pending", model_.c_str());
    return true;
}

void HamlibRadio::stop_worker_() {
    if (!worker_running_.exchange(false)) return;  // already stopped
    queue_cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    // Drain remaining queued commands; complete pending CmdFlush promises so
    // flush() callers don't block forever.
    std::lock_guard<std::mutex> lk(queue_mtx_);
    while (!cmd_queue_.empty()) {
        auto& cmd = cmd_queue_.front();
        if (std::holds_alternative<CmdFlush>(cmd))
            std::get<CmdFlush>(cmd).done->set_value();
        cmd_queue_.pop_front();
    }
}

void HamlibRadio::shutdown() {
    stop_worker_();  // safety net if stop() was not called first
    pal::log_info("HamlibRadio", "shutdown: model=%s port=%s", model_.c_str(), port_.c_str());
    if (rig_) {
        if (ready_.load()) rig_close(rig_);  // only send "q" when rig_open() succeeded
        rig_cleanup(rig_);
        rig_ = nullptr;
    }
    transmitting_.store(false);
    ready_.store(false);
}

bool HamlibRadio::start() {
    if (!rig_) return false;

    const int ret = rig_open(rig_);
    if (ret != RIG_OK) {
        pal::log_error("HamlibRadio", "rig_open failed (model=%s port=%s): %s",
                       model_.c_str(), port_.c_str(), rigerror(ret));
        return false;
    }

    // Over TCP/netrigctl: shorten the hamlib cache TTL so rig_get_freq/
    // rig_get_mode queries made ≥500 ms after the last set go live to the
    // rigctld server instead of returning hamlib's cached intended value. The
    // 2 s VFO_GET poll interval exceeds 500 ms, guaranteeing a live read on
    // every GUI sync cycle. Serial backends keep default TTL (no override —
    // cache churn on serial CAT is undesirable).
    if (!is_serial_port()) {
        rig_set_cache_timeout_ms(rig_, HAMLIB_CACHE_ALL, 500);
        // Mode reads must be LIVE (no cache). The deferred background verify
        // (sync_from_radio) reads the mode back to detect an SDR front-end's
        // (Quisk's) async band-memory restore and re-assert openALE's
        // intended mode; a cached read would just echo the value we set and
        // defeat that detection. (assert_mode() itself no longer reads back
        // — it's a single force — but sync_from_radio does, so live TTL still matters.)
        rig_set_cache_timeout_ms(rig_, HAMLIB_CACHE_MODE, 0);

        // hamlib's rig_set_mode() starts with an UNINITIALIZED `int
        // locked_mode`, fills it via rig_get_lock_mode() (return code
        // ignored), and silently returns RIG_OK WITHOUT transmitting when
        // it's nonzero. Over netrigctl the \get_lock_mode transaction fails
        // against servers that don't implement it (Quisk: "RPRT -4"; sscanf
        // on that buffer writes nothing), so whether ANY mode command
        // reaches the radio depends on stack garbage (hamlib 4.5 rig.c:2218;
        // still present in upstream master rig.c:2812). Nulling the backend
        // hook makes rig_get_lock_mode() fall back to rig->state.lock_mode —
        // a real, zero-initialized field — so the elision path is
        // deterministically dead and every rig_set_mode() transmits. Bonus:
        // removes one wire round-trip per mode set (scan path gets faster).
        rig_->caps->get_lock_mode = nullptr;
        rig_->state.lock_mode = 0;
    }

    // RF-power capability check (RIG_LEVEL_RFPOWER) — pure capability lookup,
    // no I/O, safe before the worker thread launches. Cached for the
    // connection's lifetime: set_power()/impl_set_channel() gate on
    // power_supported_ (SET) so an unsupported rig is never silently sent a
    // power command it ignores (RF-safety — see
    // IRadio::supports_power_control()). power_readback_supported_ (GET) is
    // checked separately (see header doc) because many real rigs support
    // setting RFPOWER over CAT but never implement reading it back; without
    // this split, impl_sync_from_radio() would poll a GET the backend never
    // advertised, failing on every ~2 s tick.
    power_supported_.store(rig_has_set_level(rig_, RIG_LEVEL_RFPOWER) != 0);
    power_readback_supported_.store(rig_has_get_level(rig_, RIG_LEVEL_RFPOWER) != 0);
    pal::log_info("HamlibRadio", "RF power control: set=%s get=%s",
                  power_supported_.load() ? "supported" : "not supported",
                  power_readback_supported_.load() ? "supported" : "not supported");

    // Relay-click workaround capability check — same shape as the RFPOWER
    // check above (pure capability lookup, no I/O, safe before the worker
    // thread launches).
    split_supported_.store(rig_ && rig_->caps && rig_->caps->set_split_vfo != nullptr);
    split_state_ = false;
    pal::log_info("HamlibRadio", "split VFO control: %s",
                  split_supported_.load() ? "supported" : "not supported");

    // Serial: set DTR/RTS line state after open, then wait stabilization_ms
    // before sending the first CAT command.
    if (is_serial_port()) {
        apply_line_policy();
        if (policy_.stabilization_ms > 0)
            std::this_thread::sleep_for(
                std::chrono::milliseconds(policy_.stabilization_ms));
    }

    // Initialise both caches to an empty channel before the worker starts,
    // so get_channel() never returns stale data from a previous connection.
    current_channel_ = Channel{};
    {
        std::lock_guard<std::mutex> lk(channel_mtx_);
        cached_channel_ = Channel{};
    }
    tunes_in_flight_.store(0);  // no tune outstanding on a fresh connection

    ready_.store(true);
    worker_running_.store(true);
    worker_ = std::thread(&HamlibRadio::worker_main, this);

    pal::log_info("HamlibRadio", "model=%s port=%s opened; async I/O worker started",
                  model_.c_str(), port_.c_str());
    return true;
}

void HamlibRadio::stop() {
    // Worker must be joined before rig_close() — the worker may be mid-call.
    stop_worker_();
    pal::log_info("HamlibRadio", "model=%s port=%s closed", model_.c_str(), port_.c_str());
    if (rig_ && ready_.load()) rig_close(rig_);  // only send "q" when rig_open() succeeded
    transmitting_.store(false);
    ready_.store(false);
}

// ── Non-blocking public interface ────────────────────────────────────────────
//
// Each method performs an optimistic cache update (visible immediately to
// get_channel()/is_transmitting() callers on the main thread), then enqueues
// the actual Hamlib command for the worker thread.

bool HamlibRadio::set_channel(const Channel& channel) {
    {
        std::lock_guard<std::mutex> lk(channel_mtx_);
        cached_channel_ = channel;
    }
    tunes_in_flight_.fetch_add(1, std::memory_order_acq_rel);  // outstanding until worker settles
    enqueue(CmdSetChannel{channel});
    return true;
}

Channel HamlibRadio::get_channel() const {
    std::lock_guard<std::mutex> lk(channel_mtx_);
    return cached_channel_;
}

bool HamlibRadio::set_frequency(uint32_t hz) {
    if (hz == 0) return false;
    {
        std::lock_guard<std::mutex> lk(channel_mtx_);
        cached_channel_.rx_frequency = hz;
        cached_channel_.tx_frequency = hz;
    }
    tunes_in_flight_.fetch_add(1, std::memory_order_acq_rel);  // outstanding until worker settles
    enqueue(CmdSetFrequency{hz});
    return true;
}

bool HamlibRadio::set_mode(RadioMode mode) {
    {
        std::lock_guard<std::mutex> lk(channel_mtx_);
        cached_channel_.rx_mode = mode;
        cached_channel_.tx_mode = mode;
    }
    tunes_in_flight_.fetch_add(1, std::memory_order_acq_rel);  // outstanding until worker settles
    enqueue(CmdSetMode{mode});
    return true;
}

bool HamlibRadio::set_power(int pct) {
    pct = std::clamp(pct, 0, 100);
    {
        std::lock_guard<std::mutex> lk(channel_mtx_);
        cached_channel_.power = pct;
    }
    enqueue(CmdSetPower{pct});
    return true;
}

bool HamlibRadio::sync_from_radio() {
    // Fire-and-forget: actual sync happens on the worker thread, cache is
    // updated there. All callers (tick_mode_verify, VFO_GET) ignore the return value.
    enqueue(CmdSync{});
    return false;
}

void HamlibRadio::flush() {
    if (!worker_running_.load()) return;
    auto p = std::make_shared<std::promise<void>>();
    auto f = p->get_future();
    enqueue(CmdFlush{std::move(p)});
    f.wait();
}

void HamlibRadio::set_ptt(bool transmit) {
    transmitting_.store(transmit);  // optimistic; impl_set_ptt() corrects on Hamlib failure
    enqueue(CmdSetPtt{transmit});
}

bool HamlibRadio::is_transmitting() const { return transmitting_.load(); }
bool HamlibRadio::is_ready()        const { return ready_.load(); }

bool HamlibRadio::is_tune_settled() const {
    return tunes_in_flight_.load(std::memory_order_acquire) == 0;
}

std::string HamlibRadio::get_port_config() const { return port_; }

bool HamlibRadio::supports_power_control() const { return power_supported_.load(); }

void HamlibRadio::register_send_callback(SendCommandCallback callback) {
    send_callback_ = std::move(callback);
}

void HamlibRadio::register_ack_callback(AckCallback callback) {
    ack_callback_ = std::move(callback);
}

void HamlibRadio::process_response(const uint8_t*, size_t) {}

void HamlibRadio::set_cat_trace_enabled(bool on) {
    cat_trace_enabled_.store(on);
    if (!on) {
        std::lock_guard<std::mutex> lk(trace_mtx_);
        trace_lines_.clear();
    }
}

std::vector<std::string> HamlibRadio::drain_cat_trace() {
    std::lock_guard<std::mutex> lk(trace_mtx_);
    std::vector<std::string> out(trace_lines_.begin(), trace_lines_.end());
    trace_lines_.clear();
    return out;
}

namespace { constexpr size_t kCatTraceCap = 200; }

void HamlibRadio::trace_cat(const char* fmt, ...) {
    if (!cat_trace_enabled_.load(std::memory_order_relaxed)) return;
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::lock_guard<std::mutex> lk(trace_mtx_);
    if (trace_lines_.size() >= kCatTraceCap) trace_lines_.pop_front();
    trace_lines_.emplace_back(buf);
}

// ── Async worker ─────────────────────────────────────────────────────────────

bool HamlibRadio::is_urgent(const RadioCommand& cmd) {
    return std::holds_alternative<CmdSetChannel>(cmd)   ||
           std::holds_alternative<CmdSetFrequency>(cmd) ||
           std::holds_alternative<CmdSetMode>(cmd)      ||
           std::holds_alternative<CmdSetPtt>(cmd);
}

void HamlibRadio::enqueue(RadioCommand cmd) {
    if (!worker_running_.load()) return;
    {
        std::lock_guard<std::mutex> lk(queue_mtx_);
        if (is_urgent(cmd)) {
            // Insert after already-queued urgent commands but ahead of the
            // first non-urgent one (CmdSync/CmdSetPower), so a scan hop or
            // PTT command never sits queued behind the ~400 ms background
            // sync_from_radio() poll — see enqueue()'s header doc. Preserves
            // relative order among urgent commands (no reordering of e.g. a PTT-off behind a PTT-on).
            auto it = std::find_if(cmd_queue_.begin(), cmd_queue_.end(),
                                    [](const RadioCommand& c) { return !is_urgent(c); });
            cmd_queue_.insert(it, std::move(cmd));
        } else {
            cmd_queue_.push_back(std::move(cmd));
        }
    }
    queue_cv_.notify_one();
}

void HamlibRadio::worker_main() {
#ifdef _WIN32
    SetThreadDescription(GetCurrentThread(), L"HamlibRadio-IO");
#endif
    while (true) {
        RadioCommand cmd;
        {
            std::unique_lock<std::mutex> lk(queue_mtx_);
            queue_cv_.wait(lk, [this]{
                return !cmd_queue_.empty() || !worker_running_.load();
            });
            if (!worker_running_.load()) break;  // exit; stop_worker_() drains the queue
            cmd = std::move(cmd_queue_.front());
            cmd_queue_.pop_front();
        }
        // Lock released before dispatch so the main thread can enqueue new
        // commands while Hamlib is blocking on the current one.
        worker_dispatch(cmd);
    }
}

void HamlibRadio::worker_dispatch(const RadioCommand& cmd) {
    if (std::holds_alternative<CmdSetChannel>(cmd)) {
        impl_set_channel(std::get<CmdSetChannel>(cmd).ch);
        tunes_in_flight_.fetch_sub(1, std::memory_order_acq_rel);  // tune settled
    } else if (std::holds_alternative<CmdSetFrequency>(cmd)) {
        impl_set_frequency(std::get<CmdSetFrequency>(cmd).hz);
        tunes_in_flight_.fetch_sub(1, std::memory_order_acq_rel);  // tune settled
    } else if (std::holds_alternative<CmdSetMode>(cmd)) {
        impl_set_mode(std::get<CmdSetMode>(cmd).mode);
        tunes_in_flight_.fetch_sub(1, std::memory_order_acq_rel);  // tune settled
    } else if (std::holds_alternative<CmdSetPower>(cmd)) {
        impl_set_power(std::get<CmdSetPower>(cmd).pct);
    } else if (std::holds_alternative<CmdSetPtt>(cmd)) {
        impl_set_ptt(std::get<CmdSetPtt>(cmd).on);
    } else if (std::holds_alternative<CmdSync>(cmd)) {
        impl_sync_from_radio();
    } else if (std::holds_alternative<CmdFlush>(cmd)) {
        std::get<CmdFlush>(cmd).done->set_value();
    }
}

// ── Blocking Hamlib implementations (worker-only) ────────────────────────────

bool HamlibRadio::impl_set_channel(const Channel& ch) {
    if (!rig_) return false;

    const bool freq_changed = ch.tx_frequency != current_channel_.tx_frequency;
    const char* mname = rig_strrmode(to_hamlib_mode(ch.tx_mode));
    pal::log_info("HamlibRadio", "set_channel: %u Hz  mode=%s  [freq: %s]",
                  ch.tx_frequency, mname ? mname : "?",
                  freq_changed ? "set" : "skipped");

    // Order: frequency FIRST, mode LAST. Some SDR front-ends (Quisk) restore
    // a per-band saved mode on a frequency change; sending mode last makes
    // openALE's channel mode authoritative for the synchronous case. The
    // single rig_set_mode in assert_mode() is the per-hop force — NO
    // synchronous readback loop (that loop was 3-8 TCP round-trips per hop
    // and is what made netrigctl scanning take 500-1000 ms instead of the
    // configured dwell). If the SDR's band restore lands async AFTER
    // assert_mode() returned, the deferred background verify
    // (ALEController::tick_mode_verify while SCANNING -> sync_from_radio,
    // fixed ~400 ms cadence) corrects it — no sleeps, no readback on the hop
    // path. Both mechanisms only work because start() neutralized hamlib's
    // get_lock_mode probe: otherwise the mode command may be silently elided
    // inside rig_set_mode() (see start()).
    // Mode is sent only when it CHANGES (or on the first hop). Over
    // netrigctl each rig_set_mode is a full TCP round-trip; forcing it every
    // hop made a same-mode scan 2 round-trips/hop (~2R), which with the
    // settle-anchored dwell doubled the per-channel period. A same-mode hop
    // is now freq-only (1R). Correctness of the skip: if the rig drifts off
    // the intended mode (Quisk's ASYNC per-band restore — which the per-hop
    // force could never catch anyway since it lands after this returns — or
    // an external operator change), the background verify
    // (tick_mode_verify -> sync_from_radio, ~400 ms while SCANNING) reads
    // the live mode and re-asserts. We still refresh last_mode_cmd_ on the
    // skip so verify's 5 s re-assert window stays armed through a long
    // same-mode scan. VFO = RIG_VFO_CURR; passband = RIG_PASSBAND_NORMAL.
    // Relay-click workaround: arm SPLIT before retuning so this RX/scan hop
    // doesn't cycle the PA's band/lowpass-filter relays; impl_set_ptt() drops
    // SPLIT again right before PTT ON so TX/sounding still switches the correct filter.
    if (policy_.avoid_relay_click && !split_state_) assert_split(true, "arming before RX/scan hop retune");

    int freq_ret = RIG_OK;
    if (freq_changed) {
        const auto t0 = std::chrono::steady_clock::now();
        freq_ret = rig_set_freq(rig_, RIG_VFO_CURR,
                                static_cast<freq_t>(ch.tx_frequency));
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        if (freq_ret != RIG_OK) {
            pal::log_error("HamlibRadio", "  rig_set_freq(%u) -> FAILED: %s",
                           ch.tx_frequency, rigerror(freq_ret));
            trace_cat("set_freq(%u) -> ERROR %s (%.0f ms)",
                      ch.tx_frequency, rigerror(freq_ret), ms);
        } else {
            pal::log_info("HamlibRadio", "  rig_set_freq: %u Hz -> OK",
                          ch.tx_frequency);
            trace_cat("set_freq(%u) -> OK (%.0f ms)", ch.tx_frequency, ms);
        }
    }

    int mode_ret = RIG_OK;
    if (!mode_ever_sent_ || ch.tx_mode != last_sent_mode_) {
        mode_ret = assert_mode(ch.tx_mode);            // first hop or mode change → force it (2R)
    } else {
        last_mode_cmd_ = std::chrono::steady_clock::now();  // same mode → skip CAT (1R), keep
    }                                                       // the reassert window armed

    // Power: only sent when it changes (per-channel power_pct, one CAT
    // round-trip saved on every same-power hop — same reasoning as the mode skip above).
    const bool power_changed = power_supported_.load() && ch.power != current_channel_.power;
    const int  prev_power    = current_channel_.power;
    int power_ret = RIG_OK;
    if (power_changed) power_ret = assert_power(ch.power);

    // Always track the intended state regardless of return codes. Over TCP,
    // hamlib can report failure even when rigctld applied the command (e.g.
    // a slow rig makes the RPRT read time out after rigctld already set it).
    current_channel_ = ch;
    // Power is the one exception: unlike freq/mode, a rig lacking RFPOWER
    // support (or a genuine CAT failure) must NOT be reported as having
    // changed power — that would mislead the operator into thinking a power
    // reduction took effect when the hardware never received it.
    if (power_changed && power_ret != RIG_OK) current_channel_.power = prev_power;

    // Update the main-thread-readable cache with the confirmed post-call state.
    {
        std::lock_guard<std::mutex> lk(channel_mtx_);
        cached_channel_ = current_channel_;
    }

    if (mode_ret != RIG_OK)
        pal::log_error("HamlibRadio", "  assert_mode(%s) -> FAILED: %s",
                       mname ? mname : "?", rigerror(mode_ret));
    return freq_ret == RIG_OK && mode_ret == RIG_OK && power_ret == RIG_OK;
}

bool HamlibRadio::impl_set_frequency(uint32_t hz) {
    if (!rig_ || hz == 0) return false;

    // Store intended mode to re-assert after frequency change: an SDR
    // front-end (Quisk) restores a per-band saved mode on freq change, so
    // openALE's mode must be authoritative — always assert it.
    const RadioMode saved_mode = current_channel_.tx_mode;

    // Relay-click workaround — see impl_set_channel() for the full rationale.
    if (policy_.avoid_relay_click && !split_state_) assert_split(true, "arming before RX/scan hop retune");

    const auto t0 = std::chrono::steady_clock::now();
    const int ret = rig_set_freq(rig_, RIG_VFO_CURR, static_cast<freq_t>(hz));
    const double set_freq_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    if (ret != RIG_OK) {
        pal::log_error("HamlibRadio", "rig_set_freq(%u) failed: %s", hz, rigerror(ret));
        trace_cat("set_freq(%u) -> ERROR %s (%.0f ms)", hz, rigerror(ret), set_freq_ms);
    } else {
        pal::log_info("HamlibRadio", "freq=%u Hz (simplex)", hz);
        trace_cat("set_freq(%u) -> OK (%.0f ms)", hz, set_freq_ms);
    }

    current_channel_.rx_frequency = hz;
    current_channel_.tx_frequency = hz;

    const char* saved_mname = rig_strrmode(to_hamlib_mode(saved_mode));
    pal::log_debug("HamlibRadio", "  set_frequency: re-asserting mode %s after freq change",
                   saved_mname ? saved_mname : "?");
    const int mode_ret = assert_mode(saved_mode);

    {
        std::lock_guard<std::mutex> lk(channel_mtx_);
        cached_channel_.rx_frequency = hz;
        cached_channel_.tx_frequency = hz;
    }

    return ret == RIG_OK && mode_ret == RIG_OK;
}

bool HamlibRadio::impl_set_mode(RadioMode mode) {
    if (!rig_) {
        pal::log_error("HamlibRadio", "set_mode: rig_ is null — not connected");
        return false;
    }

    const char* mname = rig_strrmode(to_hamlib_mode(mode));
    pal::log_debug("HamlibRadio", "set_mode: requesting %s", mname ? mname : "?");
    const int ret = assert_mode(mode);
    current_channel_.rx_mode = mode;
    current_channel_.tx_mode = mode;
    {
        std::lock_guard<std::mutex> lk(channel_mtx_);
        cached_channel_.rx_mode = mode;
        cached_channel_.tx_mode = mode;
    }
    if (ret != RIG_OK)
        pal::log_error("HamlibRadio", "set_mode: %s -> FAILED: %s",
                       mname ? mname : "?", rigerror(ret));
    return ret == RIG_OK;
}

bool HamlibRadio::impl_set_power(int pct) {
    if (!rig_) return false;
    const int ret = assert_power(pct);
    // Unlike freq/mode (which track "intended state" through transient
    // netrigctl failures), only trust the new value on RIG_OK: assert_power()
    // returns a dedicated failure when the rig lacks RFPOWER support, and
    // get_channel()/the GUI must keep reporting the last real value rather
    // than a power level that was never actually applied.
    if (ret == RIG_OK) {
        current_channel_.power = pct;
        std::lock_guard<std::mutex> lk(channel_mtx_);
        cached_channel_.power = pct;
    }
    return ret == RIG_OK;
}

void HamlibRadio::impl_set_ptt(bool on) {
    if (!rig_) { transmitting_.store(false); return; }

    ptt_t ptt_mode = RIG_PTT_OFF;
    if (on) {
        switch (policy_.ptt_input) {
        case SerialLinePolicy::PttInput::MIC:  ptt_mode = RIG_PTT_ON_MIC;  break;
        case SerialLinePolicy::PttInput::DATA: ptt_mode = RIG_PTT_ON_DATA; break;
        default:                               ptt_mode = RIG_PTT_ON;     break;
        }
    }

    // Relay-click workaround: drop SPLIT right before PTT ON so TX/sounding
    // switches the PA's band/lowpass filter for the actual TX frequency;
    // impl_set_channel()/impl_set_frequency() re-arm SPLIT on the next RX
    // retune once PTT goes back off (below).
    if (policy_.avoid_relay_click && on && split_state_) assert_split(false, "dropping before PTT ON / TX");

    const auto t0 = std::chrono::steady_clock::now();
    const int ptt_ret = rig_set_ptt(rig_, RIG_VFO_CURR, ptt_mode);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    if (ptt_ret == RIG_OK) {
        transmitting_.store(on);  // confirm optimistic value
        pal::log_info("HamlibRadio", on ? "PTT ON (transmitting)" : "PTT OFF (receiving)");
        trace_cat("set_ptt(%s) -> OK (%.0f ms)", on ? "ON" : "OFF", ms);
    } else {
        transmitting_.store(false);  // revert optimistic store — Hamlib call failed
        pal::log_error("HamlibRadio", "rig_set_ptt(%s) failed", on ? "ON" : "OFF");
        trace_cat("set_ptt(%s) -> ERROR (%.0f ms)", on ? "ON" : "OFF", ms);
    }

    if (policy_.avoid_relay_click && !on) assert_split(true, "re-arming after PTT OFF / back to RX");
}

bool HamlibRadio::impl_sync_from_radio() {
    if (!rig_ || !ready_.load()) return false;

    freq_t freq = 0;
    {
        const auto t0 = std::chrono::steady_clock::now();
        const int ret = rig_get_freq(rig_, RIG_VFO_CURR, &freq);
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        if (ret != RIG_OK) {
            trace_cat("get_freq -> ERROR %s (%.0f ms)", rigerror(ret), ms);
            return false;
        }
        trace_cat("get_freq -> %.0f Hz (%.0f ms)", static_cast<double>(freq), ms);
    }

    rmode_t mode = RIG_MODE_NONE;
    pbwidth_t bw  = 0;
    {
        const auto t0 = std::chrono::steady_clock::now();
        const int ret = rig_get_mode(rig_, RIG_VFO_CURR, &mode, &bw);
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        if (ret != RIG_OK) {
            trace_cat("get_mode -> ERROR %s (%.0f ms)", rigerror(ret), ms);
            return false;
        }
        const char* mname = rig_strrmode(mode);
        trace_cat("get_mode -> %s (%.0f ms)", mname ? mname : "?", ms);
    }

    const auto new_freq = static_cast<uint32_t>(freq);
    bool changed = false;
    if (new_freq != current_channel_.tx_frequency) {
        pal::log_info("HamlibRadio", "sync_from_radio: external retune %u->%u Hz",
                      current_channel_.tx_frequency, new_freq);
        current_channel_.rx_frequency = current_channel_.tx_frequency = new_freq;
        {
            std::lock_guard<std::mutex> lk(channel_mtx_);
            cached_channel_.rx_frequency = cached_channel_.tx_frequency = new_freq;
        }
        changed = true;
    }

    // NOTE: do NOT update current_channel_.tx_mode/rx_mode with the radio's
    // actual mode — current_channel_ represents openALE's intended state,
    // not what the radio reports.
    const RadioMode new_mode = from_hamlib_mode(mode);
    if (new_mode != current_channel_.tx_mode) {
        const char* actual_mname = rig_strrmode(mode);
        const char* intend_mname = rig_strrmode(to_hamlib_mode(current_channel_.tx_mode));
        pal::log_debug("HamlibRadio", "sync_from_radio: mode mismatch — radio=%s  intended=%s",
                       actual_mname ? actual_mname : "?", intend_mname ? intend_mname : "?");
        // Backstop correction: Quisk's per-band saved-mode restore fires
        // ASYNCHRONOUSLY after a frequency change and can land after both
        // assert_mode()'s readback loop AND the immediate set_mode()
        // re-assertion in step_channel()/set_vfo_channel() already returned.
        // Re-send the intended mode here, but only when:
        //  (a) the radio is still on the intended frequency (`!changed` — an
        //      external retune means the operator took over; don't fight), and
        //  (b) we commanded a mode recently (window) — a mode the operator
        //      deliberately changes on the rig later stays untouched.
        // Single fire-and-forget send, no readback loop: the next sync tick
        // (2 s) re-checks, and the window bounds any pathological ping-pong.
        // last_mode_cmd_ is deliberately NOT re-stamped here, so the window
        // can't be extended indefinitely by our own corrections.
        if (!changed &&
            std::chrono::steady_clock::now() - last_mode_cmd_ < MODE_REASSERT_WINDOW) {
            const rmode_t target = to_hamlib_mode(current_channel_.tx_mode);
            const int ret = rig_set_mode(rig_, RIG_VFO_CURR, target, RIG_PASSBAND_NORMAL);
            pal::log_debug("HamlibRadio", "sync_from_radio: re-asserting %s -> %s",
                           intend_mname ? intend_mname : "?",
                           ret == RIG_OK ? "OK" : rigerror(ret));
        } else {
            pal::log_debug("HamlibRadio", changed
                ? "sync_from_radio: freq also changed — not fighting external retune"
                : "sync_from_radio: window expired — not re-asserting");
        }
    }

    // RF power readback — the "reflects the actual/current value" half of
    // power control. Gated on power_readback_supported_ (GET), NOT
    // power_supported_ (SET) — see header doc: a rig can accept power
    // commands over CAT while never implementing readback, and polling a GET
    // it never advertised would fail on every sync tick.
    if (power_readback_supported_.load()) {
        value_t val{};
        const auto t0 = std::chrono::steady_clock::now();
        const int ret = rig_get_level(rig_, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, &val);
        const double ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        if (ret != RIG_OK) {
            trace_cat("get_level(RFPOWER) -> ERROR %s (%.0f ms)", rigerror(ret), ms);
        } else {
            const int new_power = std::clamp(static_cast<int>(val.f * 100.0f + 0.5f), 0, 100);
            trace_cat("get_level(RFPOWER) -> %d%% (%.0f ms)", new_power, ms);
            if (new_power != current_channel_.power) {
                pal::log_info("HamlibRadio", "sync_from_radio: external power change %d%%->%d%%",
                              current_channel_.power, new_power);
                current_channel_.power = new_power;
                {
                    std::lock_guard<std::mutex> lk(channel_mtx_);
                    cached_channel_.power = new_power;
                }
                changed = true;
            }
        }
    }

    return changed;
}

// Send the channel mode exactly ONCE — the authoritative per-hop force.
// Deliberately a single rig_set_mode with NO synchronous readback loop: over
// netrigctl each rig_get_mode is a full TCP round-trip (~80-150 ms), so a
// 3-pass readback/re-send loop added 3-8 round-trips per scan hop and blew
// the 200 ms dwell up to 500-1000 ms. The readback loop only ever
// compensated for two things now fixed elsewhere:
//   - hamlib's get_lock_mode elision bug — neutralized in start() (caps hook
//     nulled), so the single M reliably reaches the wire;
//   - Quisk's async per-band mode restore — lands AFTER any synchronous
//     readback would have returned anyway, so the loop couldn't catch it.
//     The deferred background verify (ALEController::tick_mode_verify while
//     SCANNING -> sync_from_radio, throttled to ~400 ms cadence) re-asserts
//     the intended mode if a revert slipped in, without touching the hop hot path.
// Freq-first/mode-last ordering means this single M also overrides Quisk's
// on-freq-change restore for the synchronous case.
//
// Worker-only: last_mode_cmd_ is accessed without a mutex — always read and
// written exclusively by the worker thread.
int HamlibRadio::assert_mode(RadioMode mode) {
    const rmode_t target = to_hamlib_mode(mode);
    const char* mname = rig_strrmode(target);

    last_mode_cmd_  = std::chrono::steady_clock::now();
    last_sent_mode_ = mode;   // track the mode on the wire so set_channel can skip unchanged hops
    mode_ever_sent_ = true;

    const auto t0 = std::chrono::steady_clock::now();
    const int mode_ret = rig_set_mode(rig_, RIG_VFO_CURR, target, RIG_PASSBAND_NORMAL);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    if (mode_ret != RIG_OK) {
        pal::log_error("HamlibRadio", "  assert_mode: rig_set_mode(%s) -> FAILED: %s",
                       mname ? mname : "?", rigerror(mode_ret));
        trace_cat("set_mode(%s) -> ERROR %s (%.0f ms)",
                  mname ? mname : "?", rigerror(mode_ret), ms);
    } else {
        pal::log_debug("HamlibRadio", "  assert_mode: rig_set_mode(%s) -> sent",
                       mname ? mname : "?");
        trace_cat("set_mode(%s) -> OK (%.0f ms)", mname ? mname : "?", ms);
    }
    return mode_ret;
}

// Send RF power exactly ONCE — power analogue of assert_mode() above, same
// fire-and-forget shape (no readback loop; readback is the separate periodic
// rig_get_level() in impl_sync_from_radio()). Returns -RIG_ENAVAIL without
// touching the wire if the connected rig doesn't advertise RFPOWER — callers
// (impl_set_power/impl_set_channel) use that distinct code to avoid ever
// reporting a power change that was never actually sent (RF-safety: see
// IRadio::supports_power_control()). Worker-only.
int HamlibRadio::assert_power(int pct) {
    if (!power_supported_.load()) {
        pal::log_warn("HamlibRadio",
                       "  assert_power: %d%% NOT sent — this rig has no RFPOWER level (unsupported)",
                       pct);
        trace_cat("set_power(%d%%) -> UNSUPPORTED (rig has no RFPOWER level)", pct);
        return -RIG_ENAVAIL;
    }

    value_t val{};
    val.f = std::clamp(pct, 0, 100) / 100.0f;

    const auto t0 = std::chrono::steady_clock::now();
    const int ret = rig_set_level(rig_, RIG_VFO_CURR, RIG_LEVEL_RFPOWER, val);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    if (ret != RIG_OK) {
        pal::log_error("HamlibRadio", "  assert_power: rig_set_level(RFPOWER, %d%%) -> FAILED: %s",
                       pct, rigerror(ret));
        trace_cat("set_level(RFPOWER, %d%%) -> ERROR %s (%.0f ms)", pct, rigerror(ret), ms);
    } else {
        pal::log_info("HamlibRadio", "  assert_power: rig_set_level(RFPOWER, %d%%) -> sent", pct);
        trace_cat("set_level(RFPOWER, %d%%) -> OK (%.0f ms)", pct, ms);
    }
    return ret;
}

// Puts the rig into (or out of) SPLIT mode — the relay-click workaround (see
// rig_avoid_relay_click doc in ale_station_config.h). No-op if the rig's
// Hamlib backend doesn't advertise split VFO control. Same fire-and-forget
// shape as assert_mode()/assert_power(): single call, no readback loop. Worker-only.
void HamlibRadio::assert_split(bool on, const char* reason) {
    if (!split_supported_.load()) return;

    const auto t0 = std::chrono::steady_clock::now();
    const int ret = rig_set_split_vfo(rig_, RIG_VFO_CURR,
                                       on ? RIG_SPLIT_ON : RIG_SPLIT_OFF, RIG_VFO_CURR);
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    split_state_ = on;
    if (ret != RIG_OK) {
        pal::log_error("HamlibRadio", "  assert_split(%s) -> FAILED: %s (%s)",
                       on ? "ON" : "OFF", rigerror(ret), reason);
        trace_cat("set_split(%s) -> ERROR %s (%.0f ms) [%s]",
                  on ? "ON" : "OFF", rigerror(ret), ms, reason);
    } else {
        pal::log_debug("HamlibRadio", "  assert_split(%s) -> sent (%s)",
                       on ? "ON" : "OFF", reason);
        trace_cat("set_split(%s) -> OK (%.0f ms) [%s]", on ? "ON" : "OFF", ms, reason);
    }
}

// ── File-static helpers (hamlib types stay out of the header) ─────────────────

static rmode_t to_hamlib_mode(pal::RadioMode mode) {
    switch (mode) {
        case pal::RadioMode::USB:      return RIG_MODE_USB;
        case pal::RadioMode::LSB:      return RIG_MODE_LSB;
        case pal::RadioMode::CW:       return RIG_MODE_CW;
        case pal::RadioMode::CW_R:     return RIG_MODE_CWR;
        case pal::RadioMode::FM:
        case pal::RadioMode::FMW:      return RIG_MODE_FM;
        case pal::RadioMode::AM:       return RIG_MODE_AM;
        case pal::RadioMode::FSK:
        case pal::RadioMode::RTTY:     return RIG_MODE_RTTY;
        case pal::RadioMode::FSK_R:    return RIG_MODE_RTTYR;
        case pal::RadioMode::DATA_USB: return RIG_MODE_PKTUSB;
        case pal::RadioMode::DATA_LSB: return RIG_MODE_PKTLSB;
        default:                       return RIG_MODE_USB;
    }
}

static pal::RadioMode from_hamlib_mode(rmode_t m) {
    switch (m) {
        case RIG_MODE_USB:    return pal::RadioMode::USB;
        case RIG_MODE_LSB:    return pal::RadioMode::LSB;
        case RIG_MODE_CW:     return pal::RadioMode::CW;
        case RIG_MODE_CWR:    return pal::RadioMode::CW_R;
        case RIG_MODE_FM:     return pal::RadioMode::FM;
        case RIG_MODE_WFM:    return pal::RadioMode::FMW;
        case RIG_MODE_AM:     return pal::RadioMode::AM;
        case RIG_MODE_RTTY:   return pal::RadioMode::RTTY;
        case RIG_MODE_RTTYR:  return pal::RadioMode::FSK_R;
        case RIG_MODE_PKTUSB: return pal::RadioMode::DATA_USB;
        case RIG_MODE_PKTLSB: return pal::RadioMode::DATA_LSB;
        default:              return pal::RadioMode::USB;
    }
}

// ── Private helpers ───────────────────────────────────────────────────────────

bool HamlibRadio::is_serial_port() const {
    // TCP/network specs start with "tcp://" or "rigctld://"
    return port_.rfind("tcp://", 0) != 0 && port_.rfind("rigctld://", 0) != 0;
}

bool HamlibRadio::configure_port() {
    if (!rig_) return false;

    // ── PTT method ────────────────────────────────────────────────────────
    // Separate from rigport (CAT connection): hamlib carries its own pttport
    // in rig_state. impl_set_ptt() needs no change — rig_set_ptt() already
    // dispatches via pttport.type.ptt configured here. The CAT-side MIC/DATA
    // sub-select (policy_.ptt_input) is unaffected by this — relevant only
    // when pttport.type.ptt == RIG_PTT_RIG (CAT).
    //
    // Set pttport.type.ptt directly rather than the top-level rig_state::ptt_type
    // convenience field: that field's presence differs across hamlib versions
    // (present in the 4.7.2 build pinned for Windows via scripts/build_hamlib.sh,
    // absent from the older hamlib package Ubuntu/odroid ships via apt) while
    // pttport.type.ptt is the field hamlib's PTT logic actually consults in
    // every version, so this stays portable across both.
    switch (ptt_policy_.type) {
        case PttPolicy::Type::RTS:  rig_->state.pttport.type.ptt = RIG_PTT_SERIAL_RTS; break;
        case PttPolicy::Type::DTR:  rig_->state.pttport.type.ptt = RIG_PTT_SERIAL_DTR; break;
        case PttPolicy::Type::NONE: rig_->state.pttport.type.ptt = RIG_PTT_NONE;       break;
        case PttPolicy::Type::CAT:
        default:                    rig_->state.pttport.type.ptt = RIG_PTT_RIG;       break;
    }
    if (ptt_policy_.type == PttPolicy::Type::RTS || ptt_policy_.type == PttPolicy::Type::DTR) {
        const std::string& ptt_dev = !ptt_policy_.port.empty() ? ptt_policy_.port : port_;
        std::strncpy(rig_->state.pttport.pathname, ptt_dev.c_str(), HAMLIB_FILPATHLEN);
        rig_->state.pttport.pathname[HAMLIB_FILPATHLEN - 1] = '\0';
        pal::log_info("HamlibRadio", "configure_port: ptt_type=%s ptt_port=%s%s",
            ptt_policy_.type == PttPolicy::Type::RTS ? "RTS" : "DTR",
            ptt_dev.c_str(), ptt_policy_.port.empty() ? " (shared with CAT port)" : "");
    } else {
        pal::log_info("HamlibRadio", "configure_port: ptt_type=%s",
            ptt_policy_.type == PttPolicy::Type::NONE ? "NONE" : "CAT");
    }

// ── Network path (rigctld via TCP) ────────────────────────────────────
    // NET_RIGCTL's netrigctl_open()/network_open() expects "host:port" — the
    // tcp:// or rigctld:// prefix must be stripped before passing it in.
    if (port_.rfind("tcp://", 0) == 0 || port_.rfind("rigctld://", 0) == 0) {
        rig_->state.rigport.type.rig = RIG_PORT_NETWORK;
        std::string endpoint = port_;
        if (endpoint.rfind("tcp://", 0) == 0)         endpoint.erase(0, 6);
        else if (endpoint.rfind("rigctld://", 0) == 0) endpoint.erase(0, 10);
        std::strncpy(rig_->state.rigport.pathname, endpoint.c_str(), HAMLIB_FILPATHLEN);
        rig_->state.rigport.pathname[HAMLIB_FILPATHLEN - 1] = '\0';
        pal::log_info("HamlibRadio", "network: endpoint=%s", endpoint.c_str());
        return true;
    }

    // ── No port (Dummy/USB/Audio backends) ────────────────────────────────
    // Empty port: leave the backend's declared port type (rig_caps default)
    // intact. Forcing RIG_PORT_SERIAL with no device would make rig_open()
    // fail for backends that don't use a port at all (Dummy, USB, Audio).
    // The unified GUI sends no device for "other" port-type models, so this
    // is the path they take; real serial rigs always carry a non-empty device string.
    if (port_.empty()) {
        pal::log_info("HamlibRadio", "configure_port: empty port — using backend default port type");
        return true;
    }

    // ── Serial path ───────────────────────────────────────────────────────
    rig_->state.rigport.type.rig = RIG_PORT_SERIAL;

    std::strncpy(rig_->state.rigport.pathname, port_.c_str(), HAMLIB_FILPATHLEN);
    rig_->state.rigport.pathname[HAMLIB_FILPATHLEN - 1] = '\0';

    // Baud rate: 0 → backend default (don't override)
    if (baud_ > 0)
        rig_->state.rigport.parm.serial.rate = baud_;

    // Data format: 8N1, no flow-control handshake.
    rig_->state.rigport.parm.serial.data_bits = 8;
    rig_->state.rigport.parm.serial.stop_bits = 1;
    rig_->state.rigport.parm.serial.parity    = RIG_PARITY_NONE;
    rig_->state.rigport.parm.serial.handshake = RIG_HANDSHAKE_NONE;

    // Set DTR/RTS as conf tokens BEFORE rig_open() (CRITICAL for TS-480 and
    // similar USB-CAT adapters): hamlib reads these values during rig_open()'s
    // DCB setup and opens the port with the correct line states.
    // apply_line_policy() sets them again AFTER rig_open() as a safety net
    // (Windows-HANDLE fallback).
    if (policy_.dtr != SerialLinePolicy::State::AUTO) {
        const char* val = (policy_.dtr == SerialLinePolicy::State::ON) ? "ON" : "OFF";
        token_t tok = rig_token_lookup(rig_, "dtr_state");
        if (tok) rig_set_conf(rig_, tok, val);
    }
    if (policy_.rts != SerialLinePolicy::State::AUTO) {
        const char* val = (policy_.rts == SerialLinePolicy::State::ON) ? "ON" : "OFF";
        token_t tok = rig_token_lookup(rig_, "rts_state");
        if (tok) rig_set_conf(rig_, tok, val);
    }

    pal::log_info("HamlibRadio",
        "configure_port: port=%s baud=%d 8N1 handshake=NONE DTR=%s RTS=%s stab=%ums",
        port_.c_str(), baud_,
        policy_.dtr == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.dtr == SerialLinePolicy::State::OFF ? "OFF" : "AUTO",
        policy_.rts == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.rts == SerialLinePolicy::State::OFF ? "OFF" : "AUTO",
        policy_.stabilization_ms);

    return true;
}

void HamlibRadio::apply_line_policy() {
    // ── Attempt 1: Hamlib token API (rig_set_conf after rig_open) ─────────
    // Works for backends that implement "dtr_state"/"rts_state". Must run
    // after rig_open(), otherwise the port is still closed.
    if (policy_.dtr != SerialLinePolicy::State::AUTO) {
        const char* val = (policy_.dtr == SerialLinePolicy::State::ON) ? "ON" : "OFF";
        token_t tok = rig_token_lookup(rig_, "dtr_state");
        if (tok) rig_set_conf(rig_, tok, val);
    }
    if (policy_.rts != SerialLinePolicy::State::AUTO) {
        const char* val = (policy_.rts == SerialLinePolicy::State::ON) ? "ON" : "OFF";
        token_t tok = rig_token_lookup(rig_, "rts_state");
        if (tok) rig_set_conf(rig_, tok, val);
    }

#ifdef _WIN32
    // ── Attempt 2 (Windows fallback): direct via hamlib's internal HANDLE ─
    // hamlib stores the HANDLE in rigport.fd as (int)(intptr_t)HANDLE. Read
    // it back with the same cast — no second CreateFile needed since hamlib
    // already opened the port exclusively.
    if (policy_.dtr == SerialLinePolicy::State::AUTO &&
        policy_.rts == SerialLinePolicy::State::AUTO) {
        pal::log_debug("HamlibRadio", "DTR/RTS: AUTO (no action)");
        return;  // nothing to do
    }

    const HANDLE h = (HANDLE)(intptr_t)rig_->state.rigport.fd;
    if (h && h != INVALID_HANDLE_VALUE) {
        if (policy_.dtr != SerialLinePolicy::State::AUTO)
            EscapeCommFunction(h,
                policy_.dtr == SerialLinePolicy::State::ON ? SETDTR : CLRDTR);
        if (policy_.rts != SerialLinePolicy::State::AUTO)
            EscapeCommFunction(h,
                policy_.rts == SerialLinePolicy::State::ON ? SETRTS : CLRRTS);
    }
#endif

    pal::log_info("HamlibRadio", "apply_line_policy: DTR=%s RTS=%s",
        policy_.dtr == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.dtr == SerialLinePolicy::State::OFF ? "OFF" : "AUTO",
        policy_.rts == SerialLinePolicy::State::ON  ? "ON"  :
        policy_.rts == SerialLinePolicy::State::OFF ? "OFF" : "AUTO");
}

// Coarse connection category from a rig's declared port type. Single source
// of truth for whether a model connects over network or serial — both the
// GUI (field visibility) and the bridge (spec grammar) derive from it.
// Everything neither network nor serial (Dummy, USB, Audio, None, …)
// collapses to "other" (no connection fields).
static std::string port_type_str(rig_port_t t) {
    switch (t) {
        case RIG_PORT_NETWORK: return "network";
        case RIG_PORT_SERIAL:  return "serial";
        default:               return "other";
    }
}

// rig_list_foreach only iterates registered backends, and
// rig_load_all_backends() is otherwise only called from
// HamlibRadio::initialize() (i.e. when a rig is actually opened). RIG_LIST/
// rig_port_type fire from the GUI before any rig is connected, so we must
// register the backends ourselves — once per process — or the dropdown stays
// empty on a fresh bridge session.
static void ensure_backends_loaded() {
    static bool backends_loaded = false;
    if (!backends_loaded) {
        rig_set_debug(RIG_DEBUG_ERR);
        rig_load_all_backends();
        backends_loaded = true;
    }
}

static int collect_rig_cb(const struct rig_caps *caps, rig_ptr_t data) {
    if (!caps) return 1;
    // macro_name is null in some Hamlib builds; fall back to model_name then rig_model number.
    const char* raw = caps->macro_name ? caps->macro_name
                    : caps->model_name ? caps->model_name
                    : nullptr;
    if (!raw) return 1;
    auto *vec = static_cast<std::vector<RigEntry>*>(data);
    std::string macro = raw;
    constexpr std::string_view prefix = "RIG_MODEL_";
    if (macro.size() > prefix.size() && macro.compare(0, prefix.size(), prefix) == 0)
        macro.erase(0, prefix.size());
    vec->push_back({(int)caps->rig_model,
                    caps->mfg_name ? caps->mfg_name : "",
                    std::move(macro),
                    port_type_str(caps->port_type)});
    return 1;
}

std::vector<RigEntry> list_rigs() {
    ensure_backends_loaded();
    std::vector<RigEntry> rigs;
    rig_list_foreach(collect_rig_cb, &rigs);
    std::sort(rigs.begin(), rigs.end(), [](const RigEntry& a, const RigEntry& b) {
        return a.mfg != b.mfg ? a.mfg < b.mfg : a.macro < b.macro;
    });
    pal::log_info("HamlibRadio", "RIG_LIST: %zu rigs found", rigs.size());
    return rigs;
}

// Finder callback for rig_port_type(): stops at the first rig whose model
// number matches the target, capturing its port type.
struct PortTypeFinder {
    int         target;
    std::string result;
};
static int find_port_type_cb(const struct rig_caps *caps, rig_ptr_t data) {
    if (!caps) return 1;
    if ((int)caps->rig_model == static_cast<PortTypeFinder*>(data)->target) {
        static_cast<PortTypeFinder*>(data)->result = port_type_str(caps->port_type);
        return 0;  // stop iterating
    }
    return 1;
}

std::string rig_port_type(int model) {
    ensure_backends_loaded();
    PortTypeFinder f{model, "other"};
    rig_list_foreach(find_port_type_cb, &f);
    return f.result;
}

} // namespace pal
