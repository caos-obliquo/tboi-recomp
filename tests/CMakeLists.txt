add_executable(isaec_tests
    test_main.cpp
    test_binding.cpp
    test_spec.cpp
    test_codec.cpp
    test_image.cpp
    test_archive.cpp
)

target_include_directories(isaec_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(isaec_tests PRIVATE isaec_core isaec_lua)

add_test(NAME isaec_tests COMMAND isaec_tests)
