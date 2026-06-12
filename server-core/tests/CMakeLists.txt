cmake_minimum_required(VERSION 3.20.0)

# Generate protobuf headers (same proto as main build)
find_package(Protobuf CONFIG REQUIRED)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/..)
protobuf_generate_cpp(TEST_PROTO_SRCS TEST_PROTO_HDRS ../../protos/client.proto)

add_executable(test-network-manager
    test_network_manager.cpp
    ../src/network_manager.cpp
    ${TEST_PROTO_SRCS}
)

# Enable test mode — replaces platform audio_manager_impl with a stub,
# so the test compiles without PipeWire (Linux) or WASAPI (Windows) dependencies.
target_compile_definitions(test-network-manager PRIVATE AUDIO_SHARE_TEST_BUILD)

target_include_directories(test-network-manager PRIVATE ../src)
set_target_properties(test-network-manager PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

find_package(asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
target_link_libraries(test-network-manager PRIVATE
    asio::asio
    spdlog::spdlog
    protobuf::libprotobuf
)

add_test(NAME network-manager-tests COMMAND test-network-manager)
