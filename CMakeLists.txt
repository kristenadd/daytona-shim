cmake_minimum_required(VERSION 3.20)
project(daytona_shim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Code shared by the DLL and the host-side tests.
add_library(shim_core STATIC shim/shell_answer.cpp shim/dyt.cpp shim/diff_runs.cpp)
target_include_directories(shim_core PUBLIC shim)

if(WIN32)
  add_library(dinput8 SHARED
    shim/main.cpp shim/log.cpp shim/iat.cpp shim/net.cpp shim/shell.cpp
    shim/dinput.cpp shim/pad.cpp shim/edition.cpp shim/restore.cpp shim/config.cpp shim/ffb.cpp shim/dinput8.def)
  set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME dinput8)
  target_compile_definitions(dinput8 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
  target_compile_options(dinput8 PRIVATE -Wall -Wno-microsoft-exception-spec)
  target_link_libraries(dinput8 PRIVATE shim_core ws2_32 dxguid)
  # SDL3's headers only: SDL3.dll is loaded at run time, when it is there.
  file(GLOB SDL3_DIRS ${CMAKE_SOURCE_DIR}/build/third_party/SDL3-*)
  list(SORT SDL3_DIRS)
  list(POP_BACK SDL3_DIRS SDL3_DIR)
  if(NOT SDL3_DIR)
    message(FATAL_ERROR "SDL3 not found: run tools/fetch_sdl.sh")
  endif()
  target_include_directories(dinput8 PRIVATE ${SDL3_DIR}/i686-w64-mingw32/include)
  # Only system DLLs at run time: libc++ and the unwinder linked in.
  target_link_options(dinput8 PRIVATE -static -Wl,--kill-at)

  # The launcher: settings and Play (launcher/).
  set(ASSETS_DIR ${CMAKE_SOURCE_DIR}/build/third_party/assets)
  if(NOT EXISTS ${ASSETS_DIR}/stb_truetype.h)
    message(FATAL_ERROR "launcher assets not found: run tools/fetch_assets.sh")
  endif()
  add_executable(DaytonaLauncher WIN32
    launcher/main.cpp launcher/settings.cpp launcher/desktop.cpp launcher/big.cpp launcher/sounds.cpp launcher/sound_bank.cpp launcher/launcher.rc
    shim/config.cpp shim/log.cpp)
  target_include_directories(DaytonaLauncher PRIVATE shim ${SDL3_DIR}/i686-w64-mingw32/include ${ASSETS_DIR})
  target_compile_definitions(DaytonaLauncher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0601)
  target_compile_options(DaytonaLauncher PRIVATE -Wall -Wno-microsoft-exception-spec)
  # The logo and icon: the generic ones in launcher/, or branding.ini's
  # (tools/branding/README.md), copied into the build for the resource compiler.
  include(cmake/branding.cmake)
  daytona_branding(logo ${CMAKE_SOURCE_DIR}/launcher/logo.png BRANDING_LOGO)
  daytona_branding(ico ${CMAKE_SOURCE_DIR}/launcher/icon.ico BRANDING_ICO)
  configure_file(${BRANDING_LOGO} ${CMAKE_BINARY_DIR}/branding/logo.png COPYONLY)
  configure_file(${BRANDING_ICO} ${CMAKE_BINARY_DIR}/branding/icon.ico COPYONLY)
  # The resource compiler finds the fonts in the assets folder, the logo and
  # icon in build/branding and the rest in launcher/.
  set_property(SOURCE launcher/launcher.rc APPEND PROPERTY COMPILE_OPTIONS
    -I${ASSETS_DIR} -I${CMAKE_BINARY_DIR}/branding -I${CMAKE_SOURCE_DIR}/launcher)
  set_property(SOURCE launcher/launcher.rc APPEND PROPERTY OBJECT_DEPENDS
    ${CMAKE_BINARY_DIR}/branding/logo.png ${CMAKE_BINARY_DIR}/branding/icon.ico)
  target_link_libraries(DaytonaLauncher PRIVATE comctl32 ${SDL3_DIR}/i686-w64-mingw32/lib/libSDL3.dll.a)
  target_link_options(DaytonaLauncher PRIVATE -static -municode)
else()
  enable_testing()
  add_executable(test_shim tests/test_shim.cpp)
  target_link_libraries(test_shim PRIVATE shim_core)
  add_test(NAME test_shim COMMAND test_shim)
  # The launcher's sound bank reader, against the player's SE.csb when
  # DAYTONA_SE_CSB names one (skipped otherwise).
  add_executable(test_sound_bank tests/test_sound_bank.cpp launcher/sound_bank.cpp)
  target_include_directories(test_sound_bank PRIVATE launcher)
  add_test(NAME test_sound_bank COMMAND test_sound_bank)
endif()
