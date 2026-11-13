cmake_minimum_required(VERSION 3.16)
project(DragonsOfFlame LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

file(GLOB DOF_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
add_executable(dragons-of-flame ${DOF_SOURCES})
target_compile_options(dragons-of-flame PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(EMSCRIPTEN)
  # Browser build: emcmake cmake -S . -B build-web && cmake --build build-web  (see web/build.sh)
  target_compile_definitions(dragons-of-flame PRIVATE DOF_SDL)
  target_compile_options(dragons-of-flame PRIVATE -sUSE_SDL=2)
  set_target_properties(dragons-of-flame PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/web SUFFIX ".html")
  # The game's files are not baked in: web/shell.html loads them from packs (web/static/packs.js) into /game and /data.
  target_link_options(dragons-of-flame PRIVATE -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sSTACK_SIZE=1MB
    -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,callMain,ENV,addRunDependency,removeRunDependency -sEXPORTED_FUNCTIONS=_main,_malloc,_free
    -sFORCE_FILESYSTEM=1 -lidbfs.js
    "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/web/shell.html")
  set_property(TARGET dragons-of-flame APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/web/shell.html)
else()
  # Native build is headless only (tests, no window): ./build/dragons-of-flame --headless ...
  set_target_properties(dragons-of-flame PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
endif()
