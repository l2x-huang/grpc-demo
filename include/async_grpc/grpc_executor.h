#pragma once

#include <thread>
#include <utility>
#include <async_grpc/grpc_context.h>
#include <async_grpc/rate.h>
#include <grpcpp/completion_queue.h>
#include <unifex/async_scope.hpp>
#include <unifex/inplace_stop_token.hpp>
#include <unifex/static_thread_pool.hpp>
#include <unifex/then.hpp>

namespace agrpc {

namespace detail {
// clang-format off
inline constexpr auto sink = [](auto&&...) {};
inline constexpr auto discard = unifex::then(sink);
// clang-format on
}  // namespace detail

class grpc_executor {
public:
    explicit grpc_executor(std::unique_ptr<grpc::CompletionQueue> cq,
                           int count = std::thread::hardware_concurrency())
      : grpc_ctx(std::move(cq))
      , pool_ctx(count) {}

    ~grpc_executor() { scope.request_stop(); }

    grpc_executor(const grpc_executor&) = delete;
    grpc_executor& operator=(const grpc_executor&) const = delete;
    grpc_executor(grpc_executor&&) = delete;
    grpc_executor& operator=(grpc_executor&&) const = delete;

    inline auto get_grpc_scheduler() { return grpc_ctx.get_scheduler(); }
    inline auto get_thread_scheduler() { return pool_ctx.get_scheduler(); }
    agrpc::grpc_context& get_grpc_context() { return grpc_ctx; }

    template <class Sender>
    inline void spawn_local(Sender&& sender) {
        scope.spawn_on(grpc_ctx.get_scheduler(),
                       detail::discard((Sender &&) sender));
    }

    template <class Sender>
    inline void spawn_blocking(Sender&& sender) {
        scope.spawn_on(pool_ctx.get_scheduler(),
                       detail::discard((Sender &&) sender));
    }

    // notice: sender return void & noexcept
    template <class Scheduler, class Sender>
    inline void spawn_on(Scheduler&& scheduler, Sender&& sender) {
        scope.spawn_on((Scheduler &&) scheduler, (Sender &&) sender);
    }

    template <class Scheduler, class Fn>
    inline void spawn_call_on(Scheduler&& scheduler, Fn&& fn) {
        scope.spawn_call_on((Scheduler &&) scheduler, (Fn &&) fn);
    }

    template <class F>
    inline auto async(F&& f) {
        return grpc_ctx.async(std::forward<F>(f));
    }

    template <class StopToken = unifex::inplace_stop_token>
    inline void run(StopToken token = {}) {
        grpc_ctx.run((StopToken &&) token);
    }

private:
    unifex::async_scope scope;
    agrpc::grpc_context grpc_ctx;
    unifex::static_thread_pool pool_ctx;
};

}  // namespace agrpc
