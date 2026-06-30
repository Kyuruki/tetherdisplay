# vdisplay module (M1.1). The mode-selection logic is portable and always built + tested; the Parsec
# VDD driver control and the hardware sandbox are Windows-only.

# Portable logic (mode selection, status strings) — compiles everywhere, unit-tested in CI/WSL.
add_library(td_vdisplay_logic STATIC src/mode_select.cpp)
target_include_directories(td_vdisplay_logic PUBLIC include)
target_compile_features(td_vdisplay_logic PUBLIC cxx_std_20)

# Portable unit tests (reuse GoogleTest discovered by the protocol module's CMake).
add_executable(vdisplay_test tests/vdisplay_test.cpp)
target_link_libraries(vdisplay_test PRIVATE td_vdisplay_logic GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(vdisplay_test)

if(WIN32)
  # Windows-only: the actual driver-touching backend + the hardware sandbox.
  add_library(td_vdisplay STATIC src/parsec_vdd.cpp)
  target_link_libraries(td_vdisplay PUBLIC td_vdisplay_logic PRIVATE setupapi user32)
  target_include_directories(td_vdisplay PUBLIC include)

  add_executable(td_vdd_sandbox sandboxes/vdd_sandbox.cpp)
  target_link_libraries(td_vdd_sandbox PRIVATE td_vdisplay)
endif()
