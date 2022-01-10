#include "async_grpc/common.h"
#include "async_grpc/grpc_executor.h"
#include "async_grpc/grpc_context.h"
#include "async_grpc/rpcs.h"
#include "unifex/inplace_stop_token.hpp"
#include "unifex/scope_guard.hpp"
#include <exception>
#include <string>
#include <thread>
#include <utility>
#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>
#include <unifex/sync_wait.hpp>
#include "helloworld/helloworld.grpc.pb.h"
#include "helloworld/helloworld.pb.h"

int main() {
    grpc::ServerBuilder builder;
    agrpc::grpc_executor ex(builder.AddCompletionQueue());
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();

    unifex::inplace_stop_source stop_source;
    std::thread th([&]() { ex.run(stop_source.get_token()); });
    unifex::scope_guard stop_on_exit = [&]() noexcept {
        // cq Always after the associated server's Shutdown()!
        // https://github.com/grpc/grpc/issues/23238#issuecomment-998680511
        server->Shutdown();
        stop_source.request_stop();
        th.join();
    };

    grpc::ChannelArguments args;
    args.SetMaxReceiveMessageSize(100 * 1024 * 1024);
    // args.SetMaxSendMessageSize(100 * 1024 * 1024);
    auto chan = grpc::CreateCustomChannel(
        "127.0.0.1:50051", grpc::InsecureChannelCredentials(), args);
    auto stub = helloworld::Greeter::NewStub(chan);

    // std::string msg =
    //     "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    // for (int i = 0; i < 18; i++) {
    //     msg += msg;
    // }
    std::string msg = "Tonic";
    helloworld::HelloRequest req;
    req.set_name(std::move(msg));
    std::cout << "req size: " << req.ByteSizeLong() << std::endl;
    auto task = agrpc::async_client_call<helloworld::HelloReply>(
        ex, &helloworld::Greeter::Stub::AsyncSayHello, stub.get(), std::move(req));
    auto r = unifex::sync_wait(std::move(task));
    try {
        auto rep = r->value();
        if (rep.ByteSizeLong() < 4000000) {
            std::cout << "got: " << rep.message() << std::endl;
        } else {
            std::cout << "got large msg" << std::endl;
        }
    } catch (std::exception& e) {
        std::cout << "got exception: " << e.what() << std::endl;
    } catch (...) {
        std::cout << "got exception" << std::endl;
    }

    return 0;
}
