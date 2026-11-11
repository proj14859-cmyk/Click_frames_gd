#pragma once
#include "Precision.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <optional>
#include <utility>

namespace cf::precision {
// Called only by the main thread. The owned worker receives value snapshots,
// never engine pointers; superseded results cannot be published to another run.
class Job {
    struct Request { std::string key; std::vector<Click> clicks; Params params; };
    std::optional<Request> pending;
    std::future<std::pair<std::string, Result>> worker;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::string desired;
    std::optional<Result> completed;
public:
    ~Job() { if (cancelled) cancelled->store(true); }
    void request(std::string key, std::vector<Click> clicks, Params params) {
        if (desired == key) return;
        if (cancelled) cancelled->store(true);
        desired = std::move(key); completed.reset();
        pending = Request{desired, std::move(clicks), params};
    }
    void cancel() {
        if (cancelled) cancelled->store(true);
        desired.clear(); pending.reset(); completed.reset();
    }
    bool poll() {
        bool published = false;
        if (worker.valid() && worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto [key, result] = worker.get();
                if (key == desired && !result.cancelled) { completed = std::move(result); published = true; }
            } catch (...) {
                // A resource failure belongs to this task, never an earlier run.
                if (!pending && !desired.empty()) { completed = Result{{}, "Precision worker failed", false}; published = true; }
            }
        }
        if (pending && !worker.valid()) {
            auto request = std::move(*pending); pending.reset();
            cancelled = std::make_shared<std::atomic_bool>(false);
            auto token = cancelled;
            try {
                worker = std::async(std::launch::async, [request = std::move(request), token]() {
                    auto result = calculate(request.clicks, request.params, [token] { return token->load(std::memory_order_relaxed); });
                    return std::pair{request.key, std::move(result)};
                });
            } catch (...) { completed = Result{{}, "Could not start precision worker", false}; published = true; }
        }
        return published;
    }
    Result const* result() const { return completed ? &*completed : nullptr; }
    bool busy() const { return !desired.empty() && !completed; }
    std::string const& key() const { return desired; }
};
}
