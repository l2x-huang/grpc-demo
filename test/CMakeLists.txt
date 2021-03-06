cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(async_grpc_tests LANGUAGES CXX)
set(LIB_NAME async_grpc)

# ---- Options ----
option(ENABLE_TEST_COVERAGE "Enable test coverage" OFF)
option(TEST_INSTALLED_VERSION "Test the version found by find_package" OFF)

# --- Import tools ----
# ---- Dependencies ----
include(tools)
include(CPM)

cpmaddpackage("gh:onqtam/doctest#2.4.5")
cpmaddpackage("gh:TheLartians/Format.cmake@1.7.0")
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)

if(TEST_INSTALLED_VERSION)
  find_package(${LIB_NAME} REQUIRED)
else()
  cpmaddpackage(NAME ${LIB_NAME} SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
endif()

# ---- Create binary ----
file(GLOB sources CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(${PROJECT_NAME} ${sources})
target_link_libraries(${PROJECT_NAME} doctest::doctest ${LIB_NAME}::${LIB_NAME} gRPC::grpc++ gRPC::gpr)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 20)

# enable compiler warnings
if(NOT TEST_INSTALLED_VERSION)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${LIB_NAME} PUBLIC -Wall -Wpedantic -Wextra -Werror)
  elseif(MSVC)
    target_compile_options(${LIB_NAME} PUBLIC /W4 /WX)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DOCTEST_CONFIG_USE_STD_HEADERS)
  endif()
endif()

# ---- Add Tests ----
enable_testing()

# Note: doctest and similar testing frameworks can automatically configure CMake tests. For other
# testing frameworks add the tests target instead: add_test(NAME greeterTests COMMAND GreeterTests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(${PROJECT_NAME})

# ---- code coverage ----
if(ENABLE_TEST_COVERAGE)
  target_compile_options(${LIB_NAME} PUBLIC -O0 -g -fprofile-arcs -ftest-coverage)
  target_link_options(${LIB_NAME} PUBLIC -fprofile-arcs -ftest-coverage)
endif()
