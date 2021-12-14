#include <doctest/doctest.h>
#include <async_grpc/version.h>

#include <string>

TEST_CASE("Lib version") {
  static_assert(std::string_view(ASYNC_GRPC_VERSION) == std::string_view("0.1.0"));
  CHECK(std::string(ASYNC_GRPC_VERSION) == std::string("0.1.0"));
}
