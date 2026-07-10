/**
 * @file events.cpp
 * @brief SimpleEventHandler — thread-safe subscription dispatch.
 *
 * Data pointers in Event::data are only valid for the synchronous duration
 * of emit() — callbacks must not store raw pointers past their return.
 *
 * @author Alex Pennington, AAM402/KY4OLB
 * @date December 2024
 * @license MIT
 */

#include "PAL/events.h"
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace pal {

namespace {

class SimpleEventHandler : public IEventHandler {
public:
    void on(EventType type, EventCallback cb) override {
        std::lock_guard<std::mutex> lk(mu_);
        per_type_[static_cast<int>(type)].push_back(std::move(cb));
    }

    void on_any(EventCallback cb) override {
        std::lock_guard<std::mutex> lk(mu_);
        any_subs_.push_back(std::move(cb));
    }

    void emit(const Event& ev) override {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = per_type_.find(static_cast<int>(ev.type));
        if (it != per_type_.end())
            for (auto& cb : it->second) cb(ev);
        for (auto& cb : any_subs_) cb(ev);
    }

    void emit(EventType type, const std::string& msg) override {
        Event ev{};
        ev.type    = type;
        ev.message = msg;
        emit(ev);
    }

private:
    std::unordered_map<int, std::vector<EventCallback>> per_type_;
    std::vector<EventCallback> any_subs_;
    std::mutex mu_;
};

} // namespace

static std::unique_ptr<IEventHandler> g_event_handler;

std::unique_ptr<IEventHandler> create_event_handler() {
    return std::make_unique<SimpleEventHandler>();
}

IEventHandler* get_event_handler() {
    return g_event_handler.get();
}

void set_event_handler(std::unique_ptr<IEventHandler> handler) {
    g_event_handler = std::move(handler);
}

} // namespace pal
