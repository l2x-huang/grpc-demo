#pragma once

#include <exception>
#include <grpcpp/support/status.h>
#include <string>

namespace agrpc {

//
// exception
//
class agrpc_ex : public std::exception {
public:
    explicit agrpc_ex(int code, std::string msg);
    int code() const;
    const char* what() const noexcept override;

private:
    int code_;
    std::string msg_;
};

std::exception_ptr make_agrpc_ex_ptr(const grpc::Status&);
std::exception_ptr make_agrpc_ex_ptr(int code, std::string msg);
void throw_agrpc_ex(int code, std::string msg);

}  // namespace agrpc
