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
else()
  enable_testing()
  add_executable(test_shim tests/test_shim.cpp)
  target_link_libraries(test_shim PRIVATE shim_core)
  add_test(NAME test_shim COMMAND test_shim)
endif()
