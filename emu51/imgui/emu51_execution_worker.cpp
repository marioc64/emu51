// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_execution_worker.h"
#include <cassert>
#include <chrono>
#include <utility>
#if defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#endif

Emu51ExecutionWorker::Access::Access(Emu51ExecutionWorker& owner, bool notify)
    : owner_(owner), notify_(notify)
{
    assert(std::this_thread::get_id() == owner_.uiThread_);
    owner_.mutex_.lock();
    ++owner_.uiDepth_;
}
Emu51ExecutionWorker::Access::~Access()
{
    if (notify_) owner_.wake_ = true;
    --owner_.uiDepth_;
    owner_.mutex_.unlock();
    if (notify_) owner_.condition_.notify_one();
}
Emu51ExecutionWorker::UiRelease::UiRelease(Emu51ExecutionWorker& owner)
    : owner_(owner), depth_(owner.uiDepth_)
{
    assert(std::this_thread::get_id() == owner_.uiThread_);
    // A UI command may have just changed Run state inside an outer guard.
    // Wake before entering a modal loop; waiting for that guard's destructor
    // would postpone execution until the dialog/editor operation returned.
    if (depth_) owner_.wake_ = true;
    owner_.uiDepth_ = 0;
    for (unsigned i = 0; i < depth_; ++i) owner_.mutex_.unlock();
    if (depth_) owner_.condition_.notify_one();
}
Emu51ExecutionWorker::UiRelease::~UiRelease()
{
    // Native modal loops may have reentered the UI while this access was
    // released. Their nested guards have ended before the original call resumes.
    assert(owner_.uiDepth_ == 0);
    for (unsigned i = 0; i < depth_; ++i) owner_.mutex_.lock();
    owner_.uiDepth_ = depth_;
}
Emu51ExecutionWorker::~Emu51ExecutionWorker() { stop(); }
void Emu51ExecutionWorker::start(std::function<bool()> service)
{
    std::lock_guard lock(mutex_);
    if (started()) return;
    stop_.store(false, std::memory_order_release);
    wake_ = true;
    // The initial lock also prevents callbacks before thread_ is assigned.
    thread_ = std::thread([this, service = std::move(service)] {
#if defined(__APPLE__)
        pthread_setname_np("emu51 CPU/audio");
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
        std::unique_lock lock(mutex_);
        while (!stop_.load(std::memory_order_acquire)) {
            wake_ = false;
            const auto started = std::chrono::steady_clock::now();
            const bool active = service();
            const auto wake = [this] { return wake_ || stop_.load(std::memory_order_acquire); };
            if (active) {
                const auto next = started + std::chrono::milliseconds(1);
                if (std::chrono::steady_clock::now() < next) condition_.wait_until(lock, next, wake);
                else { lock.unlock(); std::this_thread::yield(); lock.lock(); }
            } else condition_.wait(lock, wake);
        }
    });
    started_.store(true, std::memory_order_release);
}
void Emu51ExecutionWorker::notify()
{
    std::lock_guard lock(mutex_);
    wake_ = true;
    condition_.notify_one();
}
void Emu51ExecutionWorker::stop()
{
    if (!thread_.joinable()) return;
    assert(std::this_thread::get_id() == uiThread_);
    stop_.store(true, std::memory_order_release);
    condition_.notify_all();
    // The caller may hold an application UI guard. Do not join while holding
    // the mutex the worker needs to finish its current batch and exit.
    auto released = releaseUi();
    thread_.join();
    started_.store(false, std::memory_order_release);
}
