# Minimal host tray UI (M6). Windows-only; needs NVENC (td_encode) + libsodium (td_crypto). The thin
# Win32 shell over the (CI-tested) streaming core.
if(WIN32 AND TARGET td_encode AND TARGET td_crypto)
  add_executable(td_tray WIN32 tray_app.cpp)
  target_link_libraries(td_tray PRIVATE
    td_core td_crypto td_vdisplay td_capture td_encode td_usbmux)
endif()
