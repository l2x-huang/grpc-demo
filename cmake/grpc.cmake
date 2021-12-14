set(gRPC_INSTALL ON CACHE BOOL "")
set(gRPC_BUILD_TESTS OFF CACHE BOOL "")

cpmaddpackage(
  NAME gRPC
  GITHUB_REPOSITORY grpc/grpc
  VERSION 1.41.0
)

if(gRPC_ADDED)
  add_library(gRPC::grpc++ ALIAS grpc++)
  add_library(gRPC::gpr ALIAS gpr)
  add_library(gRPC::grpc ALIAS grpc)
  add_library(gRPC::grpc++_reflection ALIAS grpc++_reflection)
  add_library(gRPC::upb ALIAS upb)
endif()
