# Portable v1 wire-protocol module + tests.
add_library(td_protocol STATIC src/wire.cpp)
target_include_directories(td_protocol PUBLIC include)
target_compile_features(td_protocol PUBLIC cxx_std_20)

# Tests use GoogleTest. Prefer a system/find_package install; if absent, fetch it.
# (FetchContent needs network ONLY at configure time on a dev machine — never at runtime,
#  and never linked into the host binary; see docs/THIRD_PARTY.md.)
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(wire_test tests/wire_test.cpp)
target_link_libraries(wire_test PRIVATE td_protocol GTest::gtest GTest::gtest_main)
target_compile_definitions(wire_test PRIVATE TD_VECTORS_DIR="${TD_VECTORS_DIR}")

include(GoogleTest)
gtest_discover_tests(wire_test)
