cmake_minimum_required(VERSION 3.20)
project(GameServer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(GameServer main.cpp)
target_include_directories(GameServer PRIVATE ${CMAKE_SOURCE_DIR}/..)
