# crypto module (M5): TOFU pairing + AEAD channel over libsodium. Portable (the secure channel/pairing/
# byte-channel build anywhere); CredentialManagerStore is Windows-only. Needs libsodium (ISC) — provide
# it via -DSODIUM_DIR=<prefix> (vcpkg: -DSODIUM_DIR=<vcpkg>/installed/<triplet>) or a system install.
find_path(SODIUM_INCLUDE_DIR sodium.h HINTS ${SODIUM_DIR}/include "$ENV{SODIUM_DIR}/include")
find_library(SODIUM_LIBRARY NAMES sodium libsodium HINTS ${SODIUM_DIR}/lib "$ENV{SODIUM_DIR}/lib")

if(NOT (SODIUM_INCLUDE_DIR AND SODIUM_LIBRARY))
  message(STATUS "libsodium not found; set -DSODIUM_DIR=<prefix> to build td_crypto (M5).")
  return()
endif()

set(TD_CRYPTO_SRC src/secure_channel.cpp src/identity_store.cpp src/secure_byte_channel.cpp)
if(WIN32)
  list(APPEND TD_CRYPTO_SRC src/credential_store.cpp)
endif()

add_library(td_crypto STATIC ${TD_CRYPTO_SRC})
# byte_channel.hpp lives in core's public headers; we use only that interface header (no link to td_core).
target_include_directories(td_crypto PUBLIC
  include ${SODIUM_INCLUDE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../core/include)
target_compile_features(td_crypto PUBLIC cxx_std_20)
target_link_libraries(td_crypto PUBLIC ${SODIUM_LIBRARY})

add_executable(crypto_test tests/secure_channel_test.cpp tests/secure_byte_channel_test.cpp)
target_link_libraries(crypto_test PRIVATE td_crypto GTest::gtest GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(crypto_test)
