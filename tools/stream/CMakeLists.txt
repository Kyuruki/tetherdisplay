# M4/M5 host live-streaming sandbox. Windows-only; needs NVENC (td_encode) and libsodium (td_crypto).
if(WIN32 AND TARGET td_encode AND TARGET td_crypto)
  add_executable(td_stream main.cpp)
  target_link_libraries(td_stream PRIVATE td_core td_crypto td_vdisplay td_capture td_encode td_usbmux)
endif()
