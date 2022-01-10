
#include "async_grpc/common.h"
#include "async_grpc/grpc_executor.h"
#include "async_grpc/grpc_context.h"
#include "async_grpc/rpcs.h"
#include <iostream>
#include <string>
#include <utility>
#include <grpcpp/grpcpp.h>
#include <helloworld/helloworld.grpc.pb.h>

int main() {
    grpc::ServerBuilder builder;
    builder.SetMaxReceiveMessageSize(100 * 1024 * 1024);
    // builder.SetMaxSendMessageSize(100 * 1024 * 1024);
    helloworld::Greeter::AsyncService service;
    agrpc::grpc_executor ex(builder.AddCompletionQueue());
    builder.AddListeningPort("0.0.0.0:50051", grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

    // helloworld server
    auto greeter_rpc =
        agrpc::async_call_data<helloworld::HelloRequest, helloworld::HelloReply>(
            ex,
            &helloworld::Greeter::AsyncService::RequestSayHello,
            &service,
            [](const grpc::ServerContext&,
               const helloworld::HelloRequest& req,
               helloworld::HelloReply& rep) -> bool {
                static int count = 0;
                std::cout << "handle rpc SayHello, count: " << ++count << std::endl;
                auto s = "hello: " + req.name();
                rep.set_message(std::move(s));
                return true;
            },
            false);
    ex.spawn_local(std::move(greeter_rpc));

    ex.run();
    return 0;
}
