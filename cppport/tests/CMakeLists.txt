# Unit tests. One executable per library keeps link times low and lets CTest
# report each GoogleTest case individually.

add_executable(gs_core_tests
    core/test_jsnumber.cpp
    core/test_strings.cpp
    core/test_gcode_parser.cpp
    core/test_gcode_interpreter.cpp
    core/test_expression.cpp
    core/test_protocol_parser.cpp
    core/test_protocol_runner.cpp
    core/test_firmware_data.cpp
    core/test_event_loop.cpp
    core/test_streaming.cpp
)
target_link_libraries(gs_core_tests PRIVATE gs::core GTest::gtest GTest::gtest_main)
gs_copy_runtime_dlls(gs_core_tests)

# Discover at test time (not build time) so discovery runs after the runtime
# DLLs have been copied next to the executable.
gtest_discover_tests(gs_core_tests DISCOVERY_MODE PRE_TEST NO_PRETTY_VALUES)
