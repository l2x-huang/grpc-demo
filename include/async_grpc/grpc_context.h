#pragma once

#include <atomic>
#include <grpcpp/alarm.h>
#include <grpcpp/grpcpp.h>
#include <unifex/config.hpp>
#include <unifex/detail/atomic_intrusive_queue.hpp>
#include <unifex/detail/intrusive_queue.hpp>
#include <unifex/get_stop_token.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/receiver_concepts.hpp>
#include <unifex/stop_token_concepts.hpp>
#include <utility>

namespace agrpc {
class grpc_context;

struct task_base {
    using execute_fn = void(task_base*, bool) noexcept;

    task_base() noexcept : enqueued_(0), next_(nullptr), execute_(nullptr) {}
    ~task_base() { UNIFEX_ASSERT(enqueued_.load() == 0); }
    void execute(bool b) noexcept { this->execute_(this, b); }

    std::atomic<int> enqueued_;
    task_base* next_;
    execute_fn* execute_;
};

class grpc_context {
public:
    template <class Func>
    class grpc_sender;
    class schedule_sender;
    class scheduler;
    using task_queue = unifex::intrusive_queue<task_base, &task_base::next_>;
    using remote_queue =
        unifex::atomic_intrusive_queue<task_base, &task_base::next_>;
    explicit grpc_context(std::unique_ptr<grpc::CompletionQueue> cq);
    ~grpc_context();

    template <class StopToken = unifex::inplace_stop_token>
    void run(StopToken = {});

    inline grpc::CompletionQueue* get_completion_queue() const noexcept {
        return completionQueue_.get();
    }
    scheduler get_scheduler() noexcept;

    template <class F>
    grpc_sender<F> async(F&& f);

private:
    bool is_running_on_io_thread() const noexcept;
    void run_impl(const bool& shouldStop);

    void schedule_impl(task_base* op);
    void schedule_local(task_base* op) noexcept;
    void schedule_local(task_queue ops) noexcept;
    void schedule_remote(task_base* op) noexcept;

    // Execute all ready-to-run items on the local queue.
    // Will not run other items that were enqueued during the execution of the
    // items that were already enqueued.
    // This bounds the amount of work to a finite amount.
    void execute_pending_local() noexcept;

    // Check if any completion queue items are available and if so add them
    // to the local queue.
    void acquire_completion_queue_items();

    // collect the contents of the remote queue and pass them to schedule_local
    //
    // Returns true if successful.
    //
    // Returns false if some other thread concurrently enqueued work to the
    // remote queue.
    bool try_schedule_local_remote_queue_contents() noexcept;

    // Signal the remote queue by grpc::Alarm.
    //
    // This should only be called after trying to enqueue() work
    // to the remoteQueue and being told that the I/O thread is
    // inactive.
    void signal_remote_queue();

private:
    bool remoteQueueReadSubmitted_;
    std::atomic<bool> isNotifing_;
    grpc::Alarm workAlarm_;
    std::unique_ptr<grpc::CompletionQueue> completionQueue_;
    task_queue localQueue_;
    remote_queue remoteQueue_;
};

template <class F>
class grpc_context::grpc_sender {
    template <typename Receiver>
    class operation : private task_base {
    public:
        void start() noexcept {
            if (!context_.is_running_on_io_thread()) {
                this->execute_ = &operation::on_schedule_complete;
                context_.schedule_remote((task_base*)this);
            } else {
                start_io();
            }
        }

    private:
        friend grpc_sender;

        template <typename Receiver2>
        explicit operation(grpc_context& context, F&& f, Receiver2&& r)
          : context_(context)
          , initiating_function_((F &&) f)
          , receiver_((Receiver2 &&) r) {}

        static void on_schedule_complete(task_base* op, bool) noexcept {
            auto& self = *static_cast<operation*>(op);
            self.start_io();
        }

        void start_io() noexcept {
            initiating_function_(this);
            this->execute_ = &execute_impl;
        }

        static void execute_impl(task_base* p, bool ok) noexcept {
            using namespace unifex;
            operation& self = *static_cast<operation*>(p);
            if constexpr (is_nothrow_receiver_of_v<Receiver>) {
                unifex::set_value(static_cast<Receiver&&>(self.receiver_), ok);
            } else {
                UNIFEX_TRY {
                    unifex::set_value(static_cast<Receiver&&>(self.receiver_),
                                      ok);
                }
                UNIFEX_CATCH(...) {
                    unifex::set_error(static_cast<Receiver&&>(self.receiver_),
                                      std::current_exception());
                }
            }
        }

        grpc_context& context_;
        F initiating_function_;
        Receiver receiver_;
    };

public:
    // clang-format off
    template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
    using value_types = Variant<Tuple<bool>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    operation<std::remove_reference_t<Receiver>> connect(Receiver&& r) && {
        return operation<std::remove_reference_t<Receiver>>{context_, (F &&)initiating_function_,
             (Receiver &&) r};
    }
    // clang-format on

private:
    friend grpc_context;
    explicit grpc_sender(grpc_context& ctx, F f) noexcept
      : context_(ctx)
      , initiating_function_((F &&) f) {}
    grpc_context& context_;
    F initiating_function_;
};

class grpc_context::schedule_sender {
    template <typename Receiver>
    class operation : private task_base {
    public:
        void start() noexcept {
            UNIFEX_TRY { context_.schedule_impl(this); }
            UNIFEX_CATCH(...) {
                unifex::set_error(static_cast<Receiver&&>(receiver_),
                                  std::current_exception());
            }
        }

    private:
        friend schedule_sender;

        template <typename Receiver2>
        explicit operation(grpc_context& context, Receiver2&& r)
          : context_(context)
          , receiver_((Receiver2 &&) r) {
            this->execute_ = &execute_impl;
        }

        static void execute_impl(task_base* p, bool) noexcept {
            using namespace unifex;
            operation& op = *static_cast<operation*>(p);
            if constexpr (!is_stop_never_possible_v<
                              stop_token_type_t<Receiver>>) {
                if (get_stop_token(op.receiver_).stop_requested()) {
                    unifex::set_done(static_cast<Receiver&&>(op.receiver_));
                    return;
                }
            }
            if constexpr (is_nothrow_receiver_of_v<Receiver>) {
                unifex::set_value(static_cast<Receiver&&>(op.receiver_));
            } else {
                UNIFEX_TRY {
                    //
                    unifex::set_value(static_cast<Receiver&&>(op.receiver_));
                }
                UNIFEX_CATCH(...) {
                    unifex::set_error(static_cast<Receiver&&>(op.receiver_),
                                      std::current_exception());
                }
            }
        }

        grpc_context& context_;
        Receiver receiver_;
    };

public:
    // clang-format off
    template <
      template <typename...> class Variant,
      template <typename...> class Tuple>
    using value_types = Variant<Tuple<>>;

    template <template <typename...> class Variant>
    using error_types = Variant<std::exception_ptr>;

    static constexpr bool sends_done = true;

    template <typename Receiver>
    operation<std::remove_reference_t<Receiver>> connect(Receiver&& r) && {
        return operation<std::remove_reference_t<Receiver>>{context_,
             (Receiver &&) r};
    }
    // clang-format on

private:
    friend scheduler;
    explicit schedule_sender(grpc_context& ctx) noexcept : context_(ctx) {}
    grpc_context& context_;
};

class grpc_context::scheduler {
public:
    scheduler(const scheduler&) noexcept = default;
    scheduler& operator=(const scheduler&) = default;
    ~scheduler()                           = default;

    schedule_sender schedule() const noexcept {
        return schedule_sender{*context_};
    }

private:
    friend grpc_context;

    /* friend std::pair<async_reader, async_writer> tag_invoke(tag_t<open_pipe>,
     */
    /*                                                         scheduler s); */

    friend bool operator==(scheduler a, scheduler b) noexcept {
        return a.context_ == b.context_;
    }
    friend bool operator!=(scheduler a, scheduler b) noexcept {
        return a.context_ != b.context_;
    }

    explicit scheduler(grpc_context& context) noexcept : context_(&context) {}

    grpc_context* context_;
};

template <typename StopToken>
void grpc_context::run(StopToken stopToken) {
    struct stop_operation : task_base {
        stop_operation() noexcept {
            this->execute_ = [](task_base* op, bool) noexcept {
                static_cast<stop_operation*>(op)->shouldStop_ = true;
            };
        }
        bool shouldStop_ = false;
    };

    stop_operation stopOp;
    auto onStopRequested = [&] {
        this->schedule_impl(&stopOp);
    };
    typename StopToken::template callback_type<decltype(onStopRequested)>
        stopCallback{std::move(stopToken), std::move(onStopRequested)};
    run_impl(stopOp.shouldStop_);
}

template <class F>
grpc_context::grpc_sender<F> grpc_context::async(F&& f) {
    return grpc_sender{*this, std::forward<F>(f)};
}

inline grpc_context::scheduler grpc_context::get_scheduler() noexcept {
    return scheduler{*this};
}

} // namespace agrpc
