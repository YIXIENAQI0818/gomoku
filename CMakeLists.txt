cmake_minimum_required(VERSION 3.16)
project(gomoku CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- 依赖 ----
find_package(Boost CONFIG REQUIRED COMPONENTS system)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)

# ---- MySQL 客户端库(阶段 2 起)----
find_library(MYSQLCLIENT_LIBRARY mysqlclient REQUIRED)
find_path(MYSQLCLIENT_INCLUDE_DIR mysql/mysql.h REQUIRED)

# ---- 可执行文件 ----
add_executable(gomoku-server
    src/main.cpp
    src/net/connection_manager.cpp
    src/net/session.cpp
    src/net/listener.cpp
    src/protocol/message.cpp
    src/protocol/message_router.cpp
    src/data/db_pool.cpp
    src/server.cpp
)

target_include_directories(gomoku-server PRIVATE include ${MYSQLCLIENT_INCLUDE_DIR})

target_link_libraries(gomoku-server PRIVATE
    Boost::system
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    ${MYSQLCLIENT_LIBRARY}
)
