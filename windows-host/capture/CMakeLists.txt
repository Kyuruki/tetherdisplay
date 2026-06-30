# capture module (M1.2). Portable policy is always built + tested; WGC/D3D11 capture, the cross-adapter
# bridge, and the sandbox are Windows-only.

add_library(td_capture_logic STATIC src/adapter_select.cpp)
target_include_directories(td_capture_logic PUBLIC include)
target_compile_features(td_capture_logic PUBLIC cxx_std_20)

add_executable(capture_test tests/capture_test.cpp)
target_link_libraries(capture_test PRIVATE td_capture_logic GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(capture_test)

if(WIN32)
  # Windows.Graphics.Capture (C++/WinRT) + D3D11. Needs cxx_std_20 and the Windows SDK WinRT headers.
  add_library(td_capture STATIC src/wgc_capture.cpp src/cross_adapter.cpp)
  target_include_directories(td_capture PUBLIC include)
  target_compile_features(td_capture PUBLIC cxx_std_20)
  # d3d11/dxgi for the device + enumeration; windowsapp for the WinRT activation/runtime.
  target_link_libraries(td_capture PUBLIC td_capture_logic PRIVATE d3d11 dxgi windowsapp)

  add_executable(td_capture_sandbox sandboxes/capture_sandbox.cpp)
  target_link_libraries(td_capture_sandbox PRIVATE td_capture)
endif()
