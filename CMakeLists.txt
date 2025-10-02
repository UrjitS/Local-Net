cmake_minimum_required(VERSION 4.0)
project(LocalNet)

set(CMAKE_CXX_STANDARD 20)

include_directories(${CMAKE_SOURCE_DIR}/protocol)
include_directories(${CMAKE_SOURCE_DIR}/encryption)
include_directories(${CMAKE_SOURCE_DIR}/routing)

add_executable(LocalNet
        main.c
        protocol/protocol.c
        encryption/encryption.c
        routing/routing.c
)