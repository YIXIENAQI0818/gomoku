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

# ---- 可执行文件 ----
add_executable(gomoku-server
    src/main.cpp
    src/connection_manager.cpp
    src/session.cpp
    src/listener.cpp
    src/message.cpp
)

target_include_directories(gomoku-server PRIVATE include)

target_link_libraries(gomoku-server PRIVATE
    Boost::system
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
