# core module (M4): session orchestration. Portable — compiles + is tested off-device (the Win32
# capture/encoder are injected via interfaces).
add_library(td_core STATIC src/control_plane.cpp src/streaming_session.cpp)
target_include_directories(td_core PUBLIC include)
# proto codec for encode/decode; the *_logic libs carry the capture/encode public headers (interfaces
# + structs) this orchestration uses.
target_link_libraries(td_core PUBLIC td_protocol td_capture_logic td_encode_logic)

add_executable(core_test tests/control_plane_test.cpp tests/streaming_session_test.cpp)
target_link_libraries(core_test PRIVATE td_core GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(core_test)
