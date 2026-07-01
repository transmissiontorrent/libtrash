cmake_minimum_required(VERSION 3.16)
project(libtrash VERSION 0.1.0 LANGUAGES CXX)

option(LIBTRASH_BUILD_TESTS "Build libtrash tests" ON)

# Default to a static library so libtrash embeds cleanly into host projects
# (e.g. via add_subdirectory) regardless of the host's BUILD_SHARED_LIBS value.
# Standalone users can still request a shared library explicitly.
option(LIBTRASH_SHARED "Build libtrash as a shared library" OFF)

if(APPLE)
  enable_language(OBJCXX)
endif()

if(LIBTRASH_SHARED)
  add_library(trash SHARED)
else()
  add_library(trash STATIC)
endif()
add_library(libtrash::trash ALIAS trash)

target_sources(trash PRIVATE src/trash.cpp)
if(WIN32)
  target_sources(trash PRIVATE src/trash_win.cpp)
elseif(APPLE)
  target_sources(trash PRIVATE src/trash_mac.mm)
  set_source_files_properties(src/trash_mac.mm PROPERTIES LANGUAGE OBJCXX)
else()
  target_sources(trash PRIVATE src/trash_nix.cpp)
endif()

target_include_directories(trash PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

target_compile_features(trash PUBLIC cxx_std_17)

if(WIN32)
  target_link_libraries(trash PRIVATE ole32 shell32)
elseif(APPLE)
  target_link_libraries(trash PRIVATE "-framework Foundation")
endif()

# The example CLI and tests are only built when libtrash is the top-level
# project (i.e. not when pulled in via add_subdirectory as a submodule).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_executable(trash_example example.cpp)
  target_link_libraries(trash_example PRIVATE libtrash::trash)

  if(LIBTRASH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
  endif()
endif()
