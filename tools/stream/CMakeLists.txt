# M4 host live-streaming sandbox. Windows-only and needs NVENC (built only when td_encode exists).
if(WIN32 AND TARGET td_encode)
  add_executable(td_stream main.cpp)
  target_link_libraries(td_stream PRIVATE td_core td_vdisplay td_capture td_encode td_usbmux)
endif()
