// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

// One simulation owner plus short, exclusive UI access at instruction-batch
// boundaries. The UI releases access for rendering its text editor, native
// dialogs and slow I/O; the worker never calls ImGui or native window APIs.
class Emu51ExecutionWorker {
public:
    class Access {
    public:
        Access(Emu51ExecutionWorker&, bool notify);
        ~Access();
        Access(const Access&) = delete;
        Access& operator=(const Access&) = delete;
    private:
        Emu51ExecutionWorker& owner_;
        bool notify_;
    };
    class UiRelease {
    public:
        explicit UiRelease(Emu51ExecutionWorker&);
        ~UiRelease();
        UiRelease(const UiRelease&) = delete;
        UiRelease& operator=(const UiRelease&) = delete;
    private:
        Emu51ExecutionWorker& owner_;
        unsigned depth_;
    };
    Emu51ExecutionWorker() = default;
    ~Emu51ExecutionWorker();
    Emu51ExecutionWorker(const Emu51ExecutionWorker&) = delete;
    Emu51ExecutionWorker& operator=(const Emu51ExecutionWorker&) = delete;
    Access access(bool notify = true) { return Access(*this, notify); }
    UiRelease releaseUi() { return UiRelease(*this); }
    void start(std::function<bool()> service);
    void stop();
    void notify();
    bool started() const { return started_.load(std::memory_order_acquire); }
    bool onWorkerThread() const { return started() && std::this_thread::get_id() == thread_.get_id(); }
private:
    std::recursive_mutex mutex_;
    std::condition_variable_any condition_;
    std::thread thread_;
    const std::thread::id uiThread_ = std::this_thread::get_id();
    unsigned uiDepth_ = 0; // UI thread only, including reentrant native dialogs
    bool wake_ = false;  // mutex_
    std::atomic<bool> stop_{false}, started_{false};
};
