#pragma once

#include <async_grpc/common.h>
#include <async_grpc/executor.h>
#include <async_grpc/grpc_context.h>
#include <async_grpc/try.h>
#include <functional>
#include <google/protobuf/message.h>
#include <grpcpp/client_context.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/async_unary_call.h>
#include <grpcpp/support/status.h>
#include <memory>
#include <optional>
#include <type_traits>
#include <unifex/just_from.hpp>
#include <unifex/on.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/task.hpp>

namespace agrpc {

// client 1:1
template <class Rep, class Rpc, class Stub, class Req>
unifex::task<Try<Rep>> async_client_call(grpc_executor& ex, Rpc rpc, Stub stub,
                                         Req req) {
    static_assert(std::is_base_of_v<google::protobuf::Message, Req>,
                  "Req expect to be `google::protobuf::Message`");
    static_assert(std::is_base_of_v<google::protobuf::Message, Rep>,
                  "Req expect to be `google::protobuf::Message`");

    grpc::ClientContext context;
    std::unique_ptr<grpc::ClientAsyncResponseReader<Rep>> responder;
    Rep rep;
    grpc::Status status;
    bool ok = co_await ex.async([&](grpc::CompletionQueue* cq, void* tag) {
        responder = (stub->*rpc)(&context, req, cq);
        responder->Finish(&rep, &status, tag);
    });

    if (!ok) {
        co_return Try<Rep>(
            make_agrpc_ex_ptr(grpc::StatusCode::UNKNOWN, "unknown"));
    }

    if (!status.ok()) {
        co_return Try<Rep>(make_agrpc_ex_ptr(status));
    }
    co_return Try<Rep>(rep);
}

// server 1:1
template <class Req, class Rep, class Rpc, class Svc>
unifex::task<void>
async_call_data(grpc_executor& ex, Rpc rpc, Svc svc,
                const std::function<bool(const Req&, Rep&)>& handle,
                bool blocking = false) {
    static_assert(std::is_base_of_v<google::protobuf::Message, Req>,
                  "Req expect to be `goolge::protobuf::Message`");
    static_assert(std::is_base_of_v<google::protobuf::Message, Rep>,
                  "Rep expect to be `goolge::protobuf::Message`");

    struct State {
        grpc::ServerContext context;
        Req request;
        Rep reply;
        grpc::Status status;
        grpc::ServerAsyncResponseWriter<Rep> writer{&context};
    };

    auto make_task = [&](std::unique_ptr<State> shared) -> unifex::task<void> {
        auto snd = unifex::just_from([&]() {
            if (handle(shared->request, shared->reply)) {
                shared->status = grpc::Status::OK;
            } else {
                shared->status =
                    grpc::Status(grpc::StatusCode::UNKNOWN, "unknown");
            }
        });

        if (blocking) {
            co_await unifex::on(ex.get_thread_scheduler(), std::move(snd));
        } else {
            co_await unifex::on(ex.get_grpc_scheduler(), std::move(snd));
        }

        co_await ex.async([&](grpc::CompletionQueue*, void* tag) {
            shared->writer.Finish(shared->reply, shared->status, tag);
        });
    };

    for (;;) {
        auto shared = std::make_unique<State>();

        bool ok = co_await ex.async([&](grpc::CompletionQueue* cq, void* tag) {
            auto _cq = (grpc::ServerCompletionQueue*)cq;
            (svc->*rpc)(&shared->context,
                        &shared->request,
                        &shared->writer,
                        _cq,
                        _cq,
                        tag);
        });

        if (ok) {
            ex.spawn_on(ex.get_grpc_scheduler(), make_task(std::move(shared)));
        }
    }
}

// client 1:M
template <class Rep, class Rpc, class Stub, class Req>
struct grpc_client_stream {
    static_assert(std::is_base_of_v<google::protobuf::Message, Req>,
                  "Req expect to be `goolge::protobuf::Message`");
    static_assert(std::is_base_of_v<google::protobuf::Message, Rep>,
                  "Rep expect to be `goolge::protobuf::Message`");

    grpc_client_stream(grpc_executor& ex, Rpc rpc, Stub stub, Req&& req)
      : ex_(ex)
      , rpc_(rpc)
      , stub_(stub)
      , req_((Req &&) req)
      , context_(std::make_unique<grpc::ClientContext>()) {
        context_->AddMetadata("__istools", "true");
        context_->AddMetadata("__node", "test");
    }

    grpc_client_stream(const grpc_client_stream&) = delete;
    grpc_client_stream& operator=(const grpc_client_stream&) = delete;
    grpc_client_stream(grpc_client_stream&&) = default;
    grpc_client_stream& operator=(grpc_client_stream&&) = default;

    grpc_executor& ex_;
    Rpc rpc_;
    Stub stub_;
    Req req_;
    Rep rep_;
    std::unique_ptr<grpc::ClientContext> context_;
    std::unique_ptr<grpc::ClientAsyncReader<Rep>> reader_ = nullptr;

    static unifex::task<std::optional<Rep>> start_next(grpc_client_stream& s) {
        auto ok =
            co_await s.ex_.async([&s](grpc::CompletionQueue* cq, void* tag) {
                s.reader_ =
                    (s.stub_->*(s.rpc_))(s.context_.get(), s.req_, cq, tag);
            });

        std::optional<Rep> r;
        if (ok) {
            r = co_await next_value(s);
        } else {
            r = co_await finish(s, false);
        }
        co_return r;
    }

    static unifex::task<std::optional<Rep>> next_value(grpc_client_stream& s) {
        auto ok =
            co_await s.ex_.async([&](grpc::CompletionQueue* cq, void* tag) {
                s.rep_.Clear();
                s.reader_->Read(&s.rep_, tag);
            });

        if (!ok) {
            auto r = co_await finish(s, true);
            co_return r;
        } else {
            co_return std::make_optional(s.rep_);
        }
    }

    static unifex::task<std::optional<Rep>> finish(grpc_client_stream& s,
                                                   bool ok = true) {
        co_await s.ex_.async([&, ok](grpc::CompletionQueue* cq, void* tag) {
            grpc::Status status =
                ok ? grpc::Status::OK : grpc::Status::CANCELLED;
            s.reader_->Finish(&status, tag);
        });
        co_return std::nullopt;
    }

    template <class Rep2, class Stub2, class Rpc2, class Req2>
    friend unifex::task<std::optional<Rep>>
    tag_invoke(unifex::tag_t<unifex::next>,
               grpc_client_stream<Rep2, Stub2, Rpc2, Req2>& s) noexcept {
        if (s.reader_ == nullptr) {
            return grpc_client_stream<Rep2, Stub2, Rpc2, Req2>::start_next(s);
        } else {
            return grpc_client_stream<Rep2, Stub2, Rpc2, Req2>::next_value(s);
        }
    }

    template <class... Args>
    friend unifex::ready_done_sender
    tag_invoke(unifex::tag_t<unifex::cleanup>,
               grpc_client_stream<Args...>&) noexcept {
        return {};
    }
};

template <class Rep2, class Rpc2, class Stub2, class Req2>
inline grpc_client_stream<Rep2, Rpc2, Stub2, Req2>
create_grpc_client_stream(grpc_executor& ctx, Rpc2 rpc, Stub2 stub, Req2&& req) {
    return {ctx, rpc, stub, (Req2 &&) req};
}

}  // namespace agrpc
