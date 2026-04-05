cmake_minimum_required(VERSION 3.16)
project(log_committer_tcp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -O2)

# ─── Library ──────────────────────────────────────────────────────────────────
add_library(tcp_server_lib
    ../tcp-server/tcp_server.cpp
    ../tcp-server/connection.cpp
    ../tcp-server/thread_pool.cpp
)
target_include_directories(tcp_server_lib PUBLIC include)
target_link_libraries(tcp_server_lib PRIVATE pthread)

# ─── Demo executable ──────────────────────────────────────────────────────────
# Use ../ to go up one level from the tcp-server folder to find main.cpp
add_executable(log_committer_tcp ../main.cpp)
target_link_libraries(log_committer_tcp PRIVATE tcp_server_lib)
