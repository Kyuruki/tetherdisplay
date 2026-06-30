# encode module (M1.3). Portable rate-control policy is always built + tested; the NVENC encoder and
# the integration sandbox are Windows-only and require the NVIDIA Video Codec SDK header.

add_library(td_encode_logic STATIC src/rate_control.cpp)
target_include_directories(td_encode_logic PUBLIC include)
target_compile_features(td_encode_logic PUBLIC cxx_std_20)

add_executable(encode_test tests/encode_test.cpp)
target_link_libraries(encode_test PRIVATE td_encode_logic GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(encode_test)

if(WIN32)
  # NVENC is header-only at build time (nvEncodeAPI.h) and loads nvEncodeAPI64.dll at runtime — there
  # is no .lib to link. Point NVENC_INCLUDE_DIR at the SDK's Interface/ dir (or nv-codec-headers).
  find_path(NVENC_HEADER nvEncodeAPI.h
            HINTS ${NVENC_INCLUDE_DIR} ${NVENC_SDK_DIR}/Interface
                  "$ENV{NVENC_SDK_DIR}/Interface")
  if(NVENC_HEADER)
    add_library(td_encode STATIC src/nvenc_encoder.cpp)
    target_include_directories(td_encode PUBLIC include PRIVATE ${NVENC_HEADER})
    target_compile_features(td_encode PUBLIC cxx_std_20)
    target_link_libraries(td_encode PUBLIC td_encode_logic PRIVATE d3d11)

    # The integration sandbox ties capture (M1.2) to encode (M1.3).
    add_executable(td_encode_sandbox sandboxes/encode_sandbox.cpp)
    target_link_libraries(td_encode_sandbox PRIVATE td_encode td_capture)
  else()
    message(STATUS "NVENC header (nvEncodeAPI.h) not found; set -DNVENC_INCLUDE_DIR=<SDK>/Interface "
                   "to build td_encode + td_encode_sandbox. Portable encode tests still build.")
  endif()
endif()
