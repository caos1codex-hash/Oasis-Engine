cmake_minimum_required(VERSION 3.20)
project(OasisEngine VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# cJSON vendored: compila como C sin warnings-as-errors propios
add_library(cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC third_party/cjson)
if(MSVC)
  target_compile_options(cjson PRIVATE /W1)
else()
  target_compile_options(cjson PRIVATE -w)
endif()

add_library(oasis_core STATIC
  src/core.cpp
  src/assets.cpp
  src/runtime.cpp
)
target_include_directories(oasis_core PUBLIC src third_party/cjson)
target_link_libraries(oasis_core PUBLIC cjson)
if(MSVC)
  target_compile_options(oasis_core PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(oasis_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_executable(oasis
  src/main.cpp
  src/renderer_win32_d3d11.cpp
)
target_link_libraries(oasis PRIVATE oasis_core)
if(MSVC)
  target_compile_options(oasis PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(oasis PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(WIN32)
  target_link_libraries(oasis PRIVATE user32 d3d11 dxgi d3dcompiler dxguid)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(oasis_tests tests/test_core.cpp)
  target_link_libraries(oasis_tests PRIVATE oasis_core)
  if(MSVC)
    target_compile_options(oasis_tests PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(oasis_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME oasis_core COMMAND oasis_tests)
  add_test(NAME oasis_cli_version COMMAND oasis version)
  add_test(NAME oasis_cli_run COMMAND oasis run --project ${CMAKE_CURRENT_SOURCE_DIR}/DemoGame --ticks 2)
  add_test(NAME oasis_cli_state COMMAND oasis state --project ${CMAKE_CURRENT_SOURCE_DIR}/DemoGame)
  add_test(NAME oasis_cli_scene_list COMMAND oasis scene list --project ${CMAKE_CURRENT_SOURCE_DIR}/DemoGame)
  add_test(NAME oasis_cli_entity_get COMMAND oasis entity get Cube --project ${CMAKE_CURRENT_SOURCE_DIR}/DemoGame)
  add_test(NAME oasis_cli_asset_list COMMAND oasis asset list --project ${CMAKE_CURRENT_SOURCE_DIR}/DemoGame)
endif()
