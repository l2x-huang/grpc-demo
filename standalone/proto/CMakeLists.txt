cmake_minimum_required(VERSION 3.15 FATAL_ERROR)
project(proto VERSION 0.1.0)

# ---- Include guards ----
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(
    FATAL_ERROR
    "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there."
  )
endif()

include(cmake/CPM.cmake)
# PackageProject.cmake will be used to make our target installable
cpmaddpackage("gh:TheLartians/PackageProject.cmake@1.8.0")
find_package(Protobuf REQUIRED)
find_package(gRPC CONFIG REQUIRED)


file(
  GLOB_RECURSE protos
  LIST_DIRECTORIES false
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  *.proto
)

# find_package(Protobuf REQUIRED)
# find_package(gRPC CONFIG REQUIRED)
set(third-party gRPC::grpc++)
message(STATUS "Using gRPC ${gRPC_VERSION}")
message(STATUS "protos: ${protos}")

set(ALL_PROTO_FILES)
include(./cmake/protoc.cmake)
foreach(proto IN LISTS protos)
  protoc_gen(${proto} OUT proto_gen_files RELATIVE_PATH ./proto)
  list(APPEND ALL_PROTO_FILES ${proto_gen_files})
endforeach()

add_library(${PROJECT_NAME} STATIC ${ALL_PROTO_FILES})
target_link_libraries(${PROJECT_NAME} PRIVATE ${third-party})
target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/proto>
  $<INSTALL_INTERFACE:include/${PROJECT_NAME}-${PROJECT_VERSION}>
)

# ---- Create an installable target ----
# this allows users to install and find the library via `find_package()`.
# the location where the project's version header will be placed should match the project's regular
# header paths
string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)

packageproject(
  NAME ${PROJECT_NAME}
  VERSION ${PROJECT_VERSION}
  NAMESPACE ${PROJECT_NAME}
  BINARY_DIR ${PROJECT_BINARY_DIR}
  INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/proto
  INCLUDE_DESTINATION include/${PROJECT_NAME}-${PROJECT_VERSION}
  VERSION_HEADER "${VERSION_HEADER_LOCATION}"
  COMPATIBILITY SameMajorVersion
  DEPENDENCIES "gRPC 1.41.0"
)
