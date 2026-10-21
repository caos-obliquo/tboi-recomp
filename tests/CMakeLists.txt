set(ISAEC_TEST_SRC
    test_main.cpp
    test_binding.cpp
    test_spec.cpp
    test_vector.cpp
    test_color.cpp
    test_posvel.cpp
    test_codec.cpp
    test_archive.cpp
)

# Only meaningful when there is a decoder to test. A build with
# ISAEC_WITH_PNG=OFF would otherwise assert against a stub that always fails.
if(ISAEC_PNG_FOUND)
  list(APPEND ISAEC_TEST_SRC test_image.cpp)
endif()

add_executable(isaec_tests ${ISAEC_TEST_SRC})

target_include_directories(isaec_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(isaec_tests PRIVATE isaec_core isaec_lua)

add_test(NAME isaec_tests COMMAND isaec_tests)
