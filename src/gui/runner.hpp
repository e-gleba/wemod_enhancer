// Background worker: one std::jthread, UI thread owns the result.
// SDL3 threadsafety: SDL_CreateProcess is safe from any thread, so the
// worker runs the child and the UI thread only polls. No detached
// threads, no data race: the worker returns a value, the UI owns the log.
#pragma once

#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
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
    ~background_runner()
    {
        request_stop();
        join();
    }

    [[nodiscard]] bool running() const noexcept
    {
        std::lock_guard lock{mutex_};
        return active_;
    }

    // Start `task` on a fresh jthread. Returns false when busy.
    // The finished thread is joined before replacement (jthread move
    // assignment over a joinable thread would terminate).
    // Stop semantics: a stop request abandons the result (poll_run
    // never sees it) but the worker still joins - the child is a
    // short-lived patcher/probe command, and there is no UI cancel
    // path that needs preemptive kill.
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
        join();
        worker_ = std::jthread([this, task = std::move(task)](std::stop_token stop) {
            // Never let an exception escape: it would call
            // std::terminate. Publish a failed result instead so the
            // log always narrates what happened.
            try {
                run_result result = task();
                std::lock_guard lock{mutex_};
                if (!stop.stop_requested()) {
                    ready_ = std::move(result);
                }
                active_ = false;
            } catch (const std::exception& e) {
                std::lock_guard lock{mutex_};
                ready_ = run_result{
                    -1, std::string{"error: background task threw: "} + e.what() +
                            "\n"};
                active_ = false;
            } catch (...) {
                std::lock_guard lock{mutex_};
                ready_ = run_result{
                    -1, "error: background task threw an unknown exception\n"};
                active_ = false;
            }
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
        if (worker_.joinable()) {
            worker_.request_stop();
        }
    }

    void join()
    {
        if (worker_.joinable()) {
            worker_.join();
        }
    }

private:
    mutable std::mutex mutex_;
    std::jthread worker_;
    std::optional<run_result> ready_;
    bool active_{false};
};

} // namespace wemod::gui
