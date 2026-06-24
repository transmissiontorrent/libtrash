cmake_minimum_required(VERSION 3.16)
project(librecycle VERSION 0.1.0 LANGUAGES CXX)

option(LIBRECYCLE_BUILD_TESTS "Build librecycle tests" ON)

# Default to a static library so librecycle embeds cleanly into host projects
# (e.g. via add_subdirectory) regardless of the host's BUILD_SHARED_LIBS value.
# Standalone users can still request a shared library explicitly.
option(LIBRECYCLE_SHARED "Build librecycle as a shared library" OFF)

if(APPLE)
  enable_language(OBJCXX)
endif()

if(LIBRECYCLE_SHARED)
  add_library(recycle SHARED)
else()
  add_library(recycle STATIC)
endif()
add_library(librecycle::recycle ALIAS recycle)

target_sources(recycle PRIVATE src/trash.cpp)
if(WIN32)
  target_sources(recycle PRIVATE src/trash_win.cpp)
elseif(APPLE)
  target_sources(recycle PRIVATE src/trash_mac.mm)
  set_source_files_properties(src/trash_mac.mm PROPERTIES LANGUAGE OBJCXX)
else()
  target_sources(recycle PRIVATE src/trash_nix.cpp)
endif()

target_include_directories(recycle PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

target_compile_features(recycle PUBLIC cxx_std_17)

if(WIN32)
  target_link_libraries(recycle PRIVATE ole32 shell32)
elseif(APPLE)
  target_link_libraries(recycle PRIVATE "-framework Foundation")
endif()

# The example CLI and tests are only built when librecycle is the top-level
# project (i.e. not when pulled in via add_subdirectory as a submodule).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(recycle_example example.cpp)
  target_link_libraries(recycle_example PRIVATE librecycle::recycle)

  if(LIBRECYCLE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
