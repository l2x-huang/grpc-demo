#include "async_grpc/executor.h"
#include "async_grpc/grpc_context.h"
#include "async_grpc/rpcs.h"
#include "helloworld/helloworld.grpc.pb.h"
#include "helloworld/helloworld.pb.h"
#include <functional>
#include <google/protobuf/any.pb.h>
#include <google/protobuf/util/json_util.h>
#include <grpcpp/alarm.h>
#include <grpcpp/grpcpp.h>
#include <iostream>
#include <string>
#include <unifex/async_scope.hpp>
#include <unifex/single_thread_context.hpp>
#include <unifex/task.hpp>
#include <unifex/then.hpp>
#include <utility>

unifex::task<void> timeout(agrpc::grpc_context& ctx, int ms) {
    std::cout << " --------------------- before " << std::endl;
    grpc::Alarm alarm;
    auto tp = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC),
                           gpr_time_from_millis(ms, GPR_TIMESPAN));
    co_await ctx.async(
        [&](grpc::CompletionQueue* cq, void* tag) { alarm.Set(cq, tp, tag); });
    std::cout << " --------------------- after " << std::endl;
}

int main() {
    grpc::ServerBuilder builder;
    helloworld::Greeter::AsyncService service;
    // agrpc::grpc_context ctx(builder.AddCompletionQueue());
    agrpc::grpc_executor ex(builder.AddCompletionQueue());
    builder.AddListeningPort("0.0.0.0:50051",
                             grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

    // alarm
    ex.spawn_on(ex.get_grpc_scheduler(), timeout(ex.get_grpc_context(), 500));

    // helloworld server
    using req_t = helloworld::HelloRequest;
    using rep_t = helloworld::HelloReply;
    auto handle = [](const req_t& req, rep_t& rep) -> bool {
        rep.set_message("Hello " + req.name());
        return true;
    };
    auto task = agrpc::async_call_data<req_t, rep_t>(
        ex,
        &helloworld::Greeter::AsyncService::RequestSayHello,
        &service,
        handle,
        true);
    ex.spawn_on(ex.get_grpc_scheduler(), std::move(task));

    ex.run();
    return 0;
}
