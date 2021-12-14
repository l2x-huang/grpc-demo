# require package (protobuf, grpc) in project's cmakelists.txt
# refer: https://stackoverflow.com/a/35697020
# find_package(Protobuf REQUIRED)
# find_package(gRPC CONFIG REQUIRED)
set(Protobuf_PROTOC_EXECUTABLE /usr/local/bin/protoc)
set(GRPC_PLUGIN_EXEC /usr/local/bin/grpc_cpp_plugin)

include(CMakeParseArguments)

function(protoc_gen file)
    set(options OPTIONAL FAST)
    set(oneValueArgs OUT RELATIVE_PATH)
    cmake_parse_arguments(PG "${options}" "${oneValueArgs}" "" ${ARGN} )

    get_filename_component(file_relative_path ${file} DIRECTORY)
    get_filename_component(proto_path "${CMAKE_CURRENT_BINARY_DIR}/${file_relative_path}" ABSOLUTE)
    get_filename_component(basename ${file} NAME_WE)
    file(MAKE_DIRECTORY ${proto_path})
    set(_proto_srcs "${proto_path}/${basename}.pb.cc")
    set(_proto_hdrs "${proto_path}/${basename}.pb.h")
    set(_grpc_srcs "${proto_path}/${basename}.grpc.pb.cc")
    set(_grpc_hdrs "${proto_path}/${basename}.grpc.pb.h")
    set(${PG_OUT} ${_proto_srcs} ${_proto_hdrs} ${_grpc_srcs} ${_grpc_hdrs} PARENT_SCOPE)

    get_filename_component(file_absolute ${file} ABSOLUTE)
    set(in_path "${CMAKE_CURRENT_SOURCE_DIR}/${PG_RELATIVE_PATH}")
    set(out_path "${CMAKE_CURRENT_BINARY_DIR}/${PG_RELATIVE_PATH}")
    if (NOT PG_RELATIVE_PATH)
        get_filename_component(file_path "${file_absolute}" DIRECTORY)
        set(in_path ${file_path})
        set(out_path ${proto_path})
    endif()

    add_custom_command(
        OUTPUT "${_proto_srcs}" "${_proto_hdrs}" "${_grpc_srcs}" "${_grpc_hdrs}"
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        ARGS --grpc_out "${out_path}"
            --cpp_out "${out_path}"
            -I "${in_path}"
            --plugin=protoc-gen-grpc="${GRPC_PLUGIN_EXEC}"
            "${file_absolute}"
        DEPENDS "${file_absolute}")
endfunction()

function(protoc_get_path file)
    set(options OPTIONAL FAST)
    set(oneValueArgs PATH)
    cmake_parse_arguments(PGP "${options}" "${oneValueArgs}" "" ${ARGN})

    get_filename_component(proto_path "${CMAKE_CURRENT_BINARY_DIR}/${file}" ABSOLUTE)
    set(${PGP_PATH} ${proto_path} PARENT_SCOPE)
endfunction()
