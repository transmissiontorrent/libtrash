if(WIN32)
  add_executable(trash_tests test_win.cpp)
elseif(APPLE)
  add_executable(trash_tests test_mac.mm)
  set_source_files_properties(test_mac.mm PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(trash_tests PRIVATE "-framework Foundation")
else()
  add_executable(trash_tests test_linux_fdo.cpp)
endif()

target_link_libraries(trash_tests PRIVATE libtrash::trash)
target_compile_features(trash_tests PRIVATE cxx_std_17)

add_test(NAME trash_tests COMMAND trash_tests)

# The Windows smoke test touches the real trash (no clean redirect
# exists). Label it so it can be excluded locally: `ctest -LE touches_real_trash`.
if(WIN32)
  set_tests_properties(trash_tests PROPERTIES LABELS "touches_real_trash")
endif()
