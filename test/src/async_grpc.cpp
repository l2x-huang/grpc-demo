#include <chrono>
#include <string>
#include <thread>
#include <async_grpc/grpc_context.h>
#include <async_grpc/version.h>
#include <doctest/doctest.h>
#include <grpcpp/alarm.h>
#include <grpcpp/grpcpp.h>
#include <unifex/just_from.hpp>
#include <unifex/let_value.hpp>
#include <unifex/scope_guard.hpp>
#include <unifex/sync_wait.hpp>
#include <unifex/task.hpp>

unifex::task<void> timeout(agrpc::grpc_context& ctx, int ms) {
    grpc::Alarm alarm;
    auto tp = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                           gpr_time_from_millis(ms, GPR_TIMESPAN));
    co_await ctx.async(
        [&](grpc::CompletionQueue* cq, void* tag) { alarm.Set(cq, tp, tag); });
}

TEST_CASE("Lib version") {
    static_assert(std::string_view(ASYNC_GRPC_VERSION)
                  == std::string_view("0.1.0"));
    CHECK(std::string(ASYNC_GRPC_VERSION) == std::string("0.1.0"));
}

TEST_CASE("grpc context") {
    grpc::ServerBuilder builder;
    agrpc::grpc_context ctx(builder.AddCompletionQueue());
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

    unifex::inplace_stop_source stop_source;
    std::thread th([&]() { ctx.run(stop_source.get_token()); });
    unifex::scope_guard stop_on_exit = [&]() noexcept {
        // cq Always after the associated server's Shutdown()!
        // https://github.com/grpc/grpc/issues/23238#issuecomment-998680511
        server->Shutdown();
        stop_source.request_stop();
        th.join();
    };

    // grpc alarm
    auto start = std::chrono::steady_clock::now();
    unifex::sync_wait(timeout(ctx, 1000));
    auto dt = std::chrono::steady_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(dt).count();
    std::cout << "run time: " << ms << std::endl;
    CHECK(abs(ms - 1000) < 3);
}