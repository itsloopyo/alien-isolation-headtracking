# The mod's pure logic, compiled straight into the test binary: no game process,
# no D3D device, no hooks. Anything that needs one of those is not testable here
# and stays out of these files by design.
add_executable(aiht_tests
    test_main.cpp
    aim_point_tests.cpp
    camera_matrix_tests.cpp
    head_transform_tests.cpp
    mapped_buffer_table_tests.cpp
    matrix_math_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/AlienIsolationHeadTracking/aim_point.cpp
    ${CMAKE_SOURCE_DIR}/src/AlienIsolationHeadTracking/camera_matrix.cpp
    ${CMAKE_SOURCE_DIR}/src/AlienIsolationHeadTracking/head_transform.cpp
)

target_include_directories(aiht_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AlienIsolationHeadTracking
)

add_test(NAME aiht_tests COMMAND aiht_tests)
