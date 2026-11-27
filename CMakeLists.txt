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
    shim/dinput.cpp shim/pad.cpp shim/edition.cpp shim/restore.cpp shim/dinput8.def)
  set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME dinput8)
  target_compile_definitions(dinput8 PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
  target_compile_options(dinput8 PRIVATE -Wall -Wno-microsoft-exception-spec)
  target_link_libraries(dinput8 PRIVATE shim_core ws2_32 dxguid)
  # Only system DLLs at run time: libc++ and the unwinder linked in.
  target_link_options(dinput8 PRIVATE -static -Wl,--kill-at)
else()
  enable_testing()
  add_executable(test_shim tests/test_shim.cpp)
  target_link_libraries(test_shim PRIVATE shim_core)
  add_test(NAME test_shim COMMAND test_shim)
endif()
