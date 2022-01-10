cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(demo LANGUAGES CXX)

# --- Import tools ----
include(tools)
include(CPM)

# ---- Dependencies ----
# PackageProject.cmake will be used to make our target installable
cpmaddpackage("gh:TheLartians/PackageProject.cmake@1.8.0")
include(unifex)
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

cpmaddpackage(NAME async_grpc SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
cpmaddpackage(NAME proto SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/proto)

# ---- Create standalone executable ----
# file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(server server.cpp)
set_target_properties(server PROPERTIES CXX_STANDARD 20)
target_link_libraries(
  server
  async_grpc::async_grpc unifex::unifex gRPC::grpc++ gRPC::gpr proto::proto
)


add_executable(client client.cpp)
set_target_properties(client PROPERTIES CXX_STANDARD 20)
target_link_libraries(
  client
  async_grpc::async_grpc unifex::unifex gRPC::grpc++ gRPC::gpr proto::proto
)
