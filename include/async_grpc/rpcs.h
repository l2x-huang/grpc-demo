#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <absl/functional/function_ref.h>
#include <async_grpc/common.h>
#include <async_grpc/grpc_context.h>
#include <async_grpc/grpc_executor.h>
#include <async_grpc/try.h>
#include <google/protobuf/message.h>
#include <grpcpp/client_context.h>
#include <grpcpp/server_context.h>
#include <grpcpp/support/async_unary_call.h>
#include <grpcpp/support/status.h>
#include <unifex/just.hpp>
#include <unifex/just_done.hpp>
#include <unifex/just_from.hpp>
#include <unifex/let_value.hpp>
#include <unifex/on.hpp>
#include <unifex/ready_done_sender.hpp>
#include <unifex/stop_if_requested.hpp>
#include <unifex/stream_concepts.hpp>
#include <unifex/tag_invoke.hpp>
#include <unifex/task.hpp>

namespace agrpc {

namespace detail {
// clang-format off
inline constexpr auto discard_handle_context = [](auto&&...) {};
// clang-format on
}  // namespace detail

// client 1:1
template <class Rep, class Rpc, class Stub, class Req>
unifex::task<Try<Rep>>
async_client_call(grpc_executor& ex,
                  Rpc rpc,
                  Stub stub,
                  Req req,
                  absl::FunctionRef<void(grpc::ClientContext&)> handle =
                      detail::discard_handle_context) {
    static_assert(std::is_base_of_v<google::protobuf::Message, Req>,
                  "Req expect to be `google::protobuf::Message`");
    static_assert(std::is_base_of_v<google::protobuf::Message, Rep>,
                  "Req expect to be `google::protobuf::Message`");

    grpc::ClientContext context;
    handle(context);
    std::unique_ptr<grpc::ClientAsyncResponseReader<Rep>> responder;
    Rep rep;
    grpc::Status status;
    bool ok = co_await ex.async([&](grpc::CompletionQueue* cq, void* tag) {
        responder = (stub->*rpc)(&context, req, cq);
        responder->Finish(&rep, &status, tag);
    });

    if (!ok) {
        co_return Try<Rep>(make_agrpc_ex_ptr(grpc::StatusCode::UNKNOWN, "unknown"));
    }

    if (!status.ok()) {
        co_return Try<Rep>(make_agrpc_ex_ptr(status));
    }
    co_return Try<Rep>(rep);
}

// client 1:M
template <class Rep, class Rpc, class Stub, class Req>
struct grpc_client_stream {
    static_assert(std::is_base_of_v<google::protobuf::Message, Req>,
                  "Req expect to be `goolge::protobuf::Message`");
    static_assert(std::is_base_of_v<google::protobuf::Message, Rep>,
                  "Rep expect to be `goolge::protobuf::Message`");

    grpc_client_stream(grpc_executor& ex,
                       Rpc rpc,
                       Stub stub,
                       Req&& req,
                       absl::FunctionRef<void(grpc::ClientContext&)> f)
      : ex_(ex)
      , rpc_(rpc)
      , stub_(stub)
      , req_((Req &&) req)
      , context_(std::make_unique<grpc::ClientContext>()) {
        f(*(context_.get()));
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
        auto ok = co_await s.ex_.async([&s](grpc::CompletionQueue* cq, void* tag) {
            s.reader_ = (s.stub_->*(s.rpc_))(s.context_.get(), s.req_, cq, tag);
        });

        std::optional<Rep> r;
        if (ok) {
            r = co_await next_value(s);
        } else {
            co_await unifex::stop();
            r = co_await finish(s, false);
        }
        co_return r;
    }

    static unifex::task<std::optional<Rep>> next_value(grpc_client_stream& s) {
        auto ok = co_await s.ex_.async([&](grpc::CompletionQueue* cq, void* tag) {
            s.rep_.Clear();
            s.reader_->Read(&s.rep_, tag);
        });

        if (ok) {
            co_return std::make_optional(s.rep_);
        } else {
            co_await unifex::stop();
            co_return std::optional<Rep>(std::nullopt);
        }
    }

    static unifex::task<std::optional<Rep>> finish(grpc_client_stream& s,
                                                   bool ok = true) {
        co_await s.ex_.async([&, ok](grpc::CompletionQueue* cq, void* tag) {
            grpc::Status status = ok ? grpc::Status::OK : grpc::Status::CANCELLED;
            s.reader_->Finish(&status, tag);
        });
        // co_return std::nullopt;
        co_return std::optional<Rep>(std::nullopt);
    }

    template <class Rep2, class Stub2, class Rpc2, class Req2>
    friend unifex::task<std::optional<Rep2>>
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

template <class Rep2, class Rpc2, class Stub2, class Req2, class F>
inline grpc_client_stream<Rep2, Rpc2, Stub2, Req2>
grpc_client_stream_create(grpc_executor& ctx,
                          Rpc2 rpc,
                          Stub2 stub,
                          Req2&& req,
                          F&& f = detail::discard_handle_context) {
    return {ctx, rpc, stub, (Req2 &&) req, std::forward<F>(f)};
}

// server 1:1
template <class Req, class Rep, class Rpc, class Svc>
unifex::task<void> async_call_data(
    grpc_executor& ex,
    Rpc rpc,
    Svc svc,
    std::function<unifex::task<bool>(const grpc::ServerContext&, const Req&, Rep&)>
        handle) {
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
        if (co_await handle(shared->context, shared->request, shared->reply)) {
            shared->status = grpc::Status::OK;
        } else {
            shared->status = grpc::Status(grpc::StatusCode::UNKNOWN, "unknown");
        }

        co_await ex.async([&](grpc::CompletionQueue*, void* tag) {
            shared->writer.Finish(shared->reply, shared->status, tag);
        });
    };

    for (;;) {
        auto shared = std::make_unique<State>();

        bool ok = co_await ex.async([&](grpc::CompletionQueue* cq, void* tag) {
            auto _cq = (grpc::ServerCompletionQueue*)cq;
            (svc->*rpc)(
                &shared->context, &shared->request, &shared->writer, _cq, _cq, tag);
        });

        if (ok) {
            ex.spawn_on(ex.get_grpc_scheduler(), make_task(std::move(shared)));
        }
    }
}

template <class Req, class Rep, class Rpc, class Svc>
unifex::task<void> async_call_data(
    grpc_executor& ex,
    Rpc rpc,
    Svc svc,
    std::function<bool(const grpc::ServerContext&, const Req&, Rep&)> handle,
    bool blocking) {
    return async_call_data<Req, Rep, Rpc, Svc>(
        ex,
        rpc,
        svc,
        [&ex, handle = std::move(handle), blocking](
            const grpc::ServerContext& ctx,
            const Req& req,
            Rep& rep) -> unifex::task<bool> {
            auto snd = unifex::just_from([&]() { return handle(ctx, req, rep); });
            if (blocking) {
                co_return co_await unifex::on(ex.get_thread_scheduler(),
                                              std::move(snd));
            } else {
                co_return co_await snd;
            }
        });
}

// server 1:M
// template <class Req, class Rep, class Rpc, class Svc>
// unifex::task<void> async_call_data_1m(grpc_executor& ex, Rpc rpc, Svc svc,
//                                       bool blocking = false) {
//                                       }
}  // namespace agrpc
