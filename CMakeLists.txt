# TetherDisplay — top-level build (canonical, cross-platform).
# On Windows this drives the host build with MSVC; the protocol module and loopback also
# build on Linux/macOS since they are pure, portable C++20 (no platform deps yet).
#
# Configure & build (Windows, from repo root):
#   cmake -S . -B build
#   cmake --build build --config Debug
#   ctest --test-dir build -C Debug --output-on-failure
cmake_minimum_required(VERSION 3.20)
project(tetherdisplay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# Path the tests use to locate the shared golden vectors.
set(TD_VECTORS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/protocol/test-vectors")

add_subdirectory(windows-host/transport/protocol)  # defines GTest targets via find/fetch
add_subdirectory(windows-host/vdisplay)            # M1.1 — reuses those GTest targets
add_subdirectory(windows-host/capture)             # M1.2 — capture + cross-adapter
add_subdirectory(tools/loopback)
