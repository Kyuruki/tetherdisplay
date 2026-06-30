# M0 in-process loopback gate. Registered with CTest so the gate runs in CI alongside units.
add_executable(td_loopback main.cpp)
target_link_libraries(td_loopback PRIVATE td_protocol)
add_test(NAME m0_loopback COMMAND td_loopback)
