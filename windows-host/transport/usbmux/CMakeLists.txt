# usbmux module (M2). Clean-room usbmux client — portable (BSD sockets / Winsock), so it builds and is
# tested against a mock daemon on any platform. Links NO libusbmuxd/libimobiledevice (GPLv3).

add_library(td_usbmux STATIC
  src/usbmux_protocol.cpp
  src/usbmux_messages.cpp
  src/usbmux_client.cpp)
target_include_directories(td_usbmux PUBLIC include)
target_compile_features(td_usbmux PUBLIC cxx_std_20)
if(WIN32)
  target_link_libraries(td_usbmux PUBLIC ws2_32)  # Winsock
endif()

# Unit + mock-daemon integration tests (the mock test is POSIX-only; framing/plist tests run anywhere).
add_executable(usbmux_test
  tests/usbmux_protocol_test.cpp
  tests/usbmux_messages_test.cpp
  tests/usbmux_client_test.cpp)
target_link_libraries(usbmux_test PRIVATE td_usbmux GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(usbmux_test)

# Host gate sandbox: streams bytes to the iPad over a usbmux tunnel.
add_executable(td_usbmux_sandbox sandboxes/usbmux_sandbox.cpp)
target_link_libraries(td_usbmux_sandbox PRIVATE td_usbmux)
