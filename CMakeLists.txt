cmake_minimum_required(VERSION 3.16)
project(libtrash VERSION 0.1.0 LANGUAGES CXX)

# Colocate the library, example, and tests so a shared build's DLL sits next to
# the executables that load it (matters on Windows). Scoped to this directory,
# so it does not disturb a host project that pulls libtrash in via add_subdirectory.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin)

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
  # Without this, an MSVC DLL exports no symbols (the header carries no
  # __declspec(dllexport)), producing an empty import library. Harmless on
  # platforms that export everything by default.
  set_target_properties(trash PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
  add_library(trash STATIC)
endif()
add_library(libtrash::trash ALIAS trash)

# trash.cpp is platform-agnostic; trash_unsupported.cpp self-guards and only
# provides a definition on platforms without a native backend.
target_sources(trash PRIVATE src/trash.cpp src/trash_unsupported.cpp)
if(WIN32)
  target_sources(trash PRIVATE src/trash_win.cpp)
elseif(APPLE)
  target_sources(trash PRIVATE src/trash_mac.mm)
  # -[NSFileManager ...] objects are ARC-managed; without ARC the backend would
  # leak an NSString per call.
  set_source_files_properties(src/trash_mac.mm PROPERTIES
    LANGUAGE OBJCXX
    COMPILE_OPTIONS "-fobjc-arc")
else()
  target_sources(trash PRIVATE src/trash_nix.cpp)
endif()

target_include_directories(trash PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

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
