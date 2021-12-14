cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(demo LANGUAGES CXX)

# --- Import tools ----
include(../cmake/tools.cmake)

# ---- Dependencies ----
include(../cmake/CPM.cmake)
# PackageProject.cmake will be used to make our target installable
cpmaddpackage("gh:TheLartians/PackageProject.cmake@1.8.0")
include(../cmake/unifex.cmake)
include(../cmake/grpc.cmake)

cpmaddpackage(NAME async_grpc SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
cpmaddpackage(NAME proto SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/proto)

# ---- Create standalone executable ----
file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(${PROJECT_NAME} ${sources})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20 OUTPUT_NAME ${PROJECT_NAME})
target_link_libraries(
  ${PROJECT_NAME}
  async_grpc::async_grpc unifex::unifex gRPC::grpc++ gRPC::gpr proto::proto
)
