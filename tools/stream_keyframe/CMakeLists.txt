# M3 host integration sandbox. Windows-only and needs the NVENC encoder (so only when td_encode was
# built, i.e. NVENC_INCLUDE_DIR was provided). Ties together vdisplay + capture + encode + protocol +
# usbmux.
if(WIN32 AND TARGET td_encode)
  add_executable(td_stream_keyframe main.cpp)
  target_link_libraries(td_stream_keyframe PRIVATE
    td_vdisplay td_capture td_encode td_protocol td_usbmux)
endif()
