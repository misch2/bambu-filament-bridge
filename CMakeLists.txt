cmake_minimum_required(VERSION 3.20)
project(bambu_bridge LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(OBN_ROOT "/home/runner/open-bamboo-networking"
    CACHE PATH "Path to open-bamboo-networking checkout")

find_package(nlohmann_json REQUIRED)

add_executable(bambu-bridge
    src/main.cpp
    ${OBN_ROOT}/tools/plugin_runner/plugin_loader.cpp
)

target_include_directories(bambu-bridge PRIVATE
    ${OBN_ROOT}/include
    ${OBN_ROOT}/tools/plugin_runner
)

target_compile_definitions(bambu-bridge PRIVATE
    ABI_VERSION=0x020802
)

target_compile_options(bambu-bridge PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(bambu-bridge PRIVATE
    pthread
    dl
    nlohmann_json::nlohmann_json
)
