#include "async_grpc/grpc_context.h"
#include "grpc/impl/codegen/gpr_types.h"
#include "grpc/support/time.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <unifex/config.hpp>
#include <unifex/scope_guard.hpp>
#include <unistd.h>
#include <utility>

#if 0
#    include <fmt/core.h>
#    define LOG(...)                                      \
        do {                                              \
            fmt::print("{}\n", fmt::format(__VA_ARGS__)); \
            fflush(stdout);                               \
        } while (0)
#else
#    define LOG(...) (void)0
#endif

namespace agrpc {

static thread_local grpc_context* kCurrentThreadContext = nullptr;

grpc_context::grpc_context(std::unique_ptr<grpc::CompletionQueue> cq)
  : remoteQueueReadSubmitted_(false)
  , isNotifing_(false)
  , completionQueue_(std::move(cq)) {
}

grpc_context::~grpc_context() {
}

bool grpc_context::is_running_on_io_thread() const noexcept {
    return this == kCurrentThreadContext;
}

void grpc_context::run_impl(const bool& shouldStop) {
    LOG("run loop started");
    auto* old_ctx         = std::exchange(kCurrentThreadContext, this);
    unifex::scope_guard g = [=]() noexcept {
        std::exchange(kCurrentThreadContext, old_ctx);
        LOG("run loop exited");
    };

    while (true) {
        // Dequeue and process local queue items (ready to run)
        execute_pending_local();

        if (shouldStop) {
            break;
        }

        // Check for remotely-queued items.
        // Only do this if we haven't submitted a poll operation for the
        // completion queue - in which case we'll just wait until we receive the
        // completion-queue item).
        if (!remoteQueueReadSubmitted_) {
            remoteQueueReadSubmitted_ =
                try_schedule_local_remote_queue_contents();
            LOG("try_schedule_local_remote_queue_contents({})",
                remoteQueueReadSubmitted_);
        }

        if (remoteQueueReadSubmitted_) {
            acquire_completion_queue_items();
        }
    }
}

void grpc_context::schedule_impl(task_base* op) {
    UNIFEX_ASSERT(op != nullptr);
    if (is_running_on_io_thread()) {
        LOG("schedule_impl - local");
        schedule_local(op);
    } else {
        LOG("schedule_impl - remote");
        schedule_remote(op);
    }
}

void grpc_context::schedule_local(task_base* op) noexcept {
    LOG("schedule_local");
    UNIFEX_ASSERT(op->execute_);
    UNIFEX_ASSERT(op->enqueued_.load() == 0);
    ++op->enqueued_;
    localQueue_.push_back(op);
}

void grpc_context::schedule_local(task_queue ops) noexcept {
    localQueue_.append(std::move(ops));
}

void grpc_context::schedule_remote(task_base* op) noexcept {
    LOG("schedule_remote");
    UNIFEX_ASSERT(op->execute_);
    UNIFEX_ASSERT(op->enqueued_.load() == 0);
    ++op->enqueued_;
    bool io_thread_was_inactive = remoteQueue_.enqueue(op);
    LOG("io thread inactive: {}", io_thread_was_inactive);
    if (io_thread_was_inactive) {
        signal_remote_queue();
    }
}

void grpc_context::execute_pending_local() noexcept {
    if (localQueue_.empty()) {
        LOG("local queue is empty");
        return;
    }

    LOG("processing local queue items");
    size_t count = 0;
    auto pending = std::move(localQueue_);
    while (!pending.empty()) {
        auto* item = pending.pop_front();

        UNIFEX_ASSERT(item->enqueued_.load() == 1);
        --item->enqueued_;
        std::exchange(item->next_, nullptr);

        item->execute(true);
        ++count;
    }

    LOG("processed {} local queue items", count);
}

void grpc_context::acquire_completion_queue_items() {
    LOG("get from completion queue");

    void* tag = nullptr;
    bool ok;

    if (!completionQueue_->Next(&tag, &ok)) {
        LOG("completion queue is shutting down.");
        exit(-1);
    }

    do {
        if (tag == (void*)&workAlarm_) {
            LOG("to read from remote queue");
            isNotifing_               = false;
            remoteQueueReadSubmitted_ = false;
            break;
        }

        auto* task = static_cast<task_base*>(tag);
        task->execute(ok);

        auto status = completionQueue_->AsyncNext(&tag, &ok,
                                                  gpr_now(GPR_CLOCK_MONOTONIC));
        if (status == grpc::CompletionQueue::NextStatus::SHUTDOWN) {
            LOG("completion queue is shutting down.");
            exit(-1);
        } else if (status == grpc::CompletionQueue::NextStatus::TIMEOUT) {
            break;
        }
    } while (true);
}

bool grpc_context::try_schedule_local_remote_queue_contents() noexcept {
    auto ops = remoteQueue_.try_mark_inactive_or_dequeue_all();
    LOG("{}", ops.empty() ? "remote queue is empty"
                          : "registered items from remote queue");
    if (!ops.empty()) {
        schedule_local(std::move(ops));
        return false;
    }
    return true;
}

void grpc_context::signal_remote_queue() {
    if (!isNotifing_.load()) {
        LOG("signal_remote_queue");
        isNotifing_ = true;
        auto tp     = gpr_now(GPR_CLOCK_MONOTONIC);
        workAlarm_.Set(completionQueue_.get(), tp, &workAlarm_);
    }
}

} // namespace agrpc
