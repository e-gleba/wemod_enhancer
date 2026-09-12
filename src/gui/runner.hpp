// Background worker: one std::jthread, UI thread owns the result.
// SDL3 threadsafety: SDL_CreateProcess is safe from any thread, so the
// worker runs the child and the UI thread only polls. No detached
// threads, no data race: the worker returns a value, the UI owns the log.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace wemod::gui
{

struct run_result final
{
    std::int32_t exit_code{-1};
    std::string output;
};

class background_runner final
{
public:
    background_runner() = default;
    background_runner(const background_runner&) = delete;
    background_runner& operator=(const background_runner&) = delete;

    [[nodiscard]] bool running() const noexcept
    {
        std::lock_guard lock{mutex_};
        return active_;
    }

    // Start `task` on a fresh jthread. Returns false when busy.
    // Previous thread is joined before replacement (jthread RAII).
    bool launch(std::function<run_result()> task)
    {
        {
            std::lock_guard lock{mutex_};
            if (active_) {
                return false;
            }
            active_ = true;
            ready_.reset();
        }
        worker_ = std::jthread([this, task = std::move(task)](std::stop_token stop) {
            run_result result = task();
            if (stop.stop_requested()) {
                std::lock_guard lock{mutex_};
                active_ = false;
                return;
            }
            std::lock_guard lock{mutex_};
            ready_ = std::move(result);
            active_ = false;
        });
        return true;
    }

    // Non-blocking take of a finished result. False when busy or empty.
    bool try_take(run_result& out)
    {
        std::lock_guard lock{mutex_};
        if (!ready_.has_value()) {
            return false;
        }
        out = std::move(*ready_);
        ready_.reset();
        return true;
    }

    void request_stop()
    {
        worker_.request_stop_source().request_stop();
    }

private:
    mutable std::mutex mutex_;
    std::jthread worker_;
    std::optional<run_result> ready_;
    bool active_{false};
};

} // namespace wemod::gui
