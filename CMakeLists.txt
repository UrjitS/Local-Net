cmake_minimum_required(VERSION 4.0)
project(LocalNet)

set(CMAKE_CXX_STANDARD 20)

include_directories(${CMAKE_SOURCE_DIR}/protocol)
include_directories(${CMAKE_SOURCE_DIR}/encryption)
include_directories(${CMAKE_SOURCE_DIR}/routing)
include_directories(${CMAKE_SOURCE_DIR}/utils)
include_directories(${CMAKE_SOURCE_DIR}/tests)

add_executable(LocalNet
        main.c
        protocol/protocol.c
        encryption/encryption.c
        routing/routing.c
        utils/utils.c
        utils/utils.h
)

# Test binary for protocol unit tests
add_executable(protocol_tests
        protocol/protocol.c
        tests/test_protocol.c
)

target_include_directories(protocol_tests PRIVATE ${CMAKE_SOURCE_DIR}/protocol)

# Test binary for routing unit tests
add_executable(routing_tests
        routing/routing.c
        tests/test_routing.c
)

target_include_directories(routing_tests PRIVATE ${CMAKE_SOURCE_DIR}/routing)
