# gs_transport: byte links to the board (serial, TCP) on Boost.Asio, and
# serial port enumeration. Separate from gs_core, which never does OS I/O.

find_package(Threads REQUIRED)

add_library(gs_transport STATIC
    src/asio_link.cpp
    src/port_list.cpp
)
add_library(gs::transport ALIAS gs_transport)

target_include_directories(gs_transport PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(gs_transport
    PUBLIC
        gs::compile_options
        gs::core
        Boost::headers
        Threads::Threads)
if(WIN32)
    target_link_libraries(gs_transport PUBLIC ws2_32 mswsock PRIVATE setupapi)
endif()

gs_precompile_headers(gs_transport
    <algorithm> <array> <atomic> <chrono> <deque> <functional> <memory>
    <string> <string_view> <thread> <vector>
    <boost/asio.hpp>)
