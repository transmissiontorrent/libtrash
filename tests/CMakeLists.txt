if(WIN32)
  add_executable(recycle_tests test_win.cpp)
elseif(APPLE)
  add_executable(recycle_tests test_mac.mm)
  set_source_files_properties(test_mac.mm PROPERTIES LANGUAGE OBJCXX)
  target_link_libraries(recycle_tests PRIVATE "-framework Foundation")
else()
  add_executable(recycle_tests test_linux_fdo.cpp)
endif()

target_link_libraries(recycle_tests PRIVATE librecycle::recycle)
target_compile_features(recycle_tests PRIVATE cxx_std_17)

add_test(NAME recycle_tests COMMAND recycle_tests)

# The Windows smoke test touches the real recycle bin (no clean redirect
# exists). Label it so it can be excluded locally: `ctest -LE touches_real_trash`.
if(WIN32)
  set_tests_properties(recycle_tests PROPERTIES LABELS "touches_real_trash")
endif()
