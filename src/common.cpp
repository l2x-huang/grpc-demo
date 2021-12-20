#include "async_grpc/common.h"
#include <exception>

namespace agrpc {

agrpc_ex::agrpc_ex(int code, std::string msg) : code_(code), msg_(msg) {
}

int agrpc_ex::code() const {
    return code_;
}

const char* agrpc_ex::what() const noexcept {
    return msg_.c_str();
}

std::exception_ptr make_agrpc_ex_ptr(int code, std::string msg) {
    return std::make_exception_ptr(agrpc_ex(code, (std::string &&) msg));
}

std::exception_ptr make_agrpc_ex_ptr(const grpc::Status& s) {
    return make_agrpc_ex_ptr(s.error_code(), s.error_message());
}

void throw_agrpc_ex(int code, std::string msg) {
    throw agrpc_ex(code, (std::string &&) msg);
}


}  // namespace agrpc
