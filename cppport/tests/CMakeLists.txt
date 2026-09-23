# Unit tests. One executable per library keeps link times low and lets CTest
# report each GoogleTest case individually.

add_executable(gs_core_tests
    core/test_jsnumber.cpp
    core/test_strings.cpp
    core/test_units.cpp
    core/test_gcode_parser.cpp
    core/test_gcode_interpreter.cpp
    core/test_expression.cpp
    core/test_protocol_parser.cpp
    core/test_protocol_runner.cpp
    core/test_firmware_data.cpp
    core/test_event_loop.cpp
    core/test_streaming.cpp
    core/test_jog.cpp
    core/test_jogging.cpp
    core/test_locations.cpp
    core/test_actions.cpp
    core/test_calibration.cpp
    core/test_controller.cpp
    core/test_session.cpp
    core/test_config.cpp
    core/test_program_analysis.cpp
    core/test_outline.cpp
    core/test_simulator.cpp
    core/test_spindle.cpp
    core/test_probing.cpp
    core/test_surfacing.cpp
    core/test_toolchange.cpp
)
target_link_libraries(gs_core_tests PRIVATE gs::core GTest::gtest GTest::gtest_main)
# Golden files generated from the upstream JavaScript (see tools/).
target_compile_definitions(gs_core_tests PRIVATE GS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
gs_precompile_headers(gs_core_tests
    <gtest/gtest.h>
    <algorithm> <cmath> <cstdint> <functional> <limits> <map> <memory>
    <optional> <string> <string_view> <utility> <vector>
    <boost/json.hpp>)
gs_copy_runtime_dlls(gs_core_tests)

# Discover at test time (not build time) so discovery runs after the runtime
# DLLs have been copied next to the executable.
gtest_discover_tests(gs_core_tests DISCOVERY_MODE PRE_TEST NO_PRETTY_VALUES)

add_executable(gs_transport_tests
    transport/test_transport.cpp
)
target_link_libraries(gs_transport_tests PRIVATE gs::transport GTest::gtest GTest::gtest_main)
gs_precompile_headers(gs_transport_tests
    <gtest/gtest.h> <chrono> <condition_variable> <deque> <functional> <mutex> <string> <thread>
    <boost/asio.hpp>)
gs_copy_runtime_dlls(gs_transport_tests)
gtest_discover_tests(gs_transport_tests DISCOVERY_MODE PRE_TEST NO_PRETTY_VALUES)

if(TARGET gs_app)
    add_executable(gs_app_tests
        app/test_app.cpp
    )
    target_link_libraries(gs_app_tests PRIVATE gs_app GTest::gtest GTest::gtest_main)
    gs_precompile_headers(gs_app_tests <gtest/gtest.h> <QtCore> <functional> <string>)
    gs_copy_runtime_dlls(gs_app_tests)
    gs_deploy_qt_plugins(gs_app_tests)
    gtest_discover_tests(gs_app_tests DISCOVERY_MODE PRE_TEST NO_PRETTY_VALUES)
endif()
