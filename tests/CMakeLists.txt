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

# The config differential test. The oracle is the newest published build's
# reader, dev's src/AlienIsolationHeadTracking/config.cpp copied into
# tests/config_differential/oracle, compiled whole into its adapter; every core
# source it included is the same file at dev's pin and now, which
# tests/config_differential/provenance.txt pins.
set(AIHT_DIFFERENTIAL_DIR ${CMAKE_SOURCE_DIR}/tests/config_differential)
add_library(aiht_config_oracle STATIC
    ${AIHT_DIFFERENTIAL_DIR}/oracle_config_adapter.cpp
    ${AIHT_DIFFERENTIAL_DIR}/oracle_hotkeys_adapter.cpp
)
target_include_directories(aiht_config_oracle PRIVATE
    ${AIHT_DIFFERENTIAL_DIR}/oracle/src
    ${AIHT_DIFFERENTIAL_DIR}
)
target_link_libraries(aiht_config_oracle PRIVATE cameraunlock)

add_executable(config_differential_tests
    ${AIHT_DIFFERENTIAL_DIR}/differential_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/AlienIsolationHeadTracking/config.cpp
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
)
target_include_directories(config_differential_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AlienIsolationHeadTracking
    ${CMAKE_SOURCE_DIR}/src
    ${AIHT_DIFFERENTIAL_DIR}
)
target_compile_definitions(config_differential_tests PRIVATE
    AIHT_DIFFERENTIAL_DATA="${AIHT_DIFFERENTIAL_DIR}/data"
    AIHT_COMMITTED_CONFIG="${CMAKE_SOURCE_DIR}/config/CameraUnlock.ini"
)
target_link_libraries(config_differential_tests PRIVATE cameraunlock aiht_config_oracle)
add_test(NAME config_differential_tests COMMAND config_differential_tests)

# Core's canonical config lint over every distinct file the migration wrote.
find_program(AIHT_NODE node)
if(AIHT_NODE)
    add_test(NAME config_lint COMMAND ${AIHT_NODE}
        ${AIHT_DIFFERENTIAL_DIR}/lint-migrated.mjs
        $<TARGET_FILE_DIR:config_differential_tests>/migrated)
    set_tests_properties(config_lint PROPERTIES DEPENDS config_differential_tests)
endif()

# The committed config, the hotkey defaults and the saves. `config_tests
# --render-config <path>` is how `pixi run render-config` rewrites the file.
add_executable(config_tests
    config_tests.cpp
    ${CMAKE_SOURCE_DIR}/src/AlienIsolationHeadTracking/config.cpp
    ${CMAKE_SOURCE_DIR}/src/legacy_config/legacy_config.cpp
)
target_include_directories(config_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AlienIsolationHeadTracking
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(config_tests PRIVATE
    AIHT_COMMITTED_CONFIG="${CMAKE_SOURCE_DIR}/config/CameraUnlock.ini"
)
target_link_libraries(config_tests PRIVATE cameraunlock)
add_test(NAME config_tests COMMAND config_tests)
