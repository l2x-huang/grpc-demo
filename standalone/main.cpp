#include "async_grpc/grpc_context.h"
#include "helloworld/helloworld.grpc.pb.h"
#include <functional>
#include <grpcpp/alarm.h>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <unifex/async_scope.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/task.hpp>
#include <unifex/then.hpp>
#include <utility>

unifex::task<void> timeout(agrpc::grpc_context& ctx, int ms) {
    std::cout << " --------------------- before " << std::endl;
    auto cq = ctx.get_completion_queue();
    grpc::Alarm alarm;
    auto tp = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                           gpr_time_from_millis(ms, GPR_TIMESPAN));
    co_await ctx.async([&](void* tag) { alarm.Set(cq, tp, tag); });
    std::cout << " --------------------- after " << std::endl;
}

int main() {
    grpc::ServerBuilder builder;
    helloworld::Greeter::AsyncService service;
    agrpc::grpc_context ctx(builder.AddCompletionQueue());
    builder.AddListeningPort("0.0.0.0:50051",
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

    unifex::async_scope scope;
    unifex::single_thread_context thread;

    // alarm
    scope.spawn_on(thread.get_scheduler(), timeout(ctx, 500));

    // helloworld server
    scope.spawn_on(ctx.get_scheduler(), [&]() mutable -> unifex::task<void> {
        for (;;) {
            grpc::ServerContext context;
            helloworld::HelloRequest request;
            grpc::ServerAsyncResponseWriter<helloworld::HelloReply> writer{
                &context};

            std::cout << "[SayHello] wait for request in" << std::endl;
            bool ok = co_await ctx.async([&](void* tag) {
                auto cq =
                    (grpc::ServerCompletionQueue*)ctx.get_completion_queue();
                service.RequestSayHello(&context, &request, &writer, cq, cq,
                                        tag);
            });
            std::cout << "[SayHello] request: " << request.name() << std::endl;
            if (ok) {
                helloworld::HelloReply response;
                response.set_message("Hello " + request.name());
                auto task = unifex::then(
                    ctx.async([writer, response](void* tag) mutable {
                        std::cout << "[SayHello] response start" << std::endl;
                        writer.Finish(response, grpc::Status::OK, tag);
                    }),
                    [](bool) {
                        std::cout << "[SayHello] response end" << std::endl;
                    });
                scope.spawn_on(ctx.get_scheduler(), task);
            }
        }
    }());

    ctx.run();
    return 0;
}
