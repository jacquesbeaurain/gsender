# gs_core: the Qt-free heart of the port - G-code interpretation, firmware
# protocol parsing, streaming and controller logic. Everything here is plain
# C++20 plus Boost so it can be unit tested with simulated time and I/O.

add_library(gs_core STATIC
    src/util/strings.cpp
    src/util/jsnumber.cpp
    src/util/units.cpp
    src/util/datetime.cpp
    src/util/zip.cpp
    src/gcode/parser.cpp
    src/gcode/interpreter.cpp
    src/expr/value.cpp
    src/expr/expression.cpp
    src/expr/builtins.cpp
    src/protocol/types.cpp
    src/protocol/line_parser.cpp
    src/protocol/runner.cpp
    src/protocol/firmware_data.cpp
    src/protocol/ymodem.cpp
    src/runtime/event_loop.cpp
    src/controller/streaming.cpp
    src/controller/jog_limits.cpp
    src/controller/jog_streamer.cpp
    src/controller/jogging.cpp
    src/controller/locations.cpp
    src/controller/helpers.cpp
    src/calibration/calibration.cpp
    src/controller/actions.cpp
    src/controller/controller.cpp
    src/controller/session.cpp
    src/controller/spindle.cpp
    src/config/json_path.cpp
    src/config/config_store.cpp
    src/config/history.cpp
    src/config/records.cpp
    src/config/machine_profiles.cpp
    src/job/program_analysis.cpp
    src/job/outline.cpp
    src/job/step_through.cpp
    src/job/accessibility.cpp
    src/probe/probing.cpp
    src/rotary/rotary.cpp
    src/surfacing/surfacing.cpp
    src/toolchange/wizards.cpp
    src/sim/grbl_simulator.cpp
)
add_library(gs::core ALIAS gs_core)

target_include_directories(gs_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(gs_core
    PUBLIC
        gs::compile_options
        Boost::headers
        Boost::json)

gs_precompile_headers(gs_core
    <algorithm> <array> <cmath> <cstdint> <deque> <functional> <limits> <map>
    <memory> <optional> <string> <string_view> <utility> <variant> <vector>
    <boost/json.hpp> <boost/regex.hpp>)

set(GS_RESOURCE_DIR "${PROJECT_SOURCE_DIR}/resources")
file(GLOB_RECURSE GS_RESOURCE_FILES CONFIGURE_DEPENDS
    "${GS_RESOURCE_DIR}/data/*.json" "${GS_RESOURCE_DIR}/images/*.png")
gs_embed_resources(gs_core BASE_DIR "${GS_RESOURCE_DIR}" FILES ${GS_RESOURCE_FILES})
