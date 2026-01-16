cmake_minimum_required(VERSION 3.22)
project(json_parser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(JSON_BUILD_TESTS "Build tests" ON)

add_library(json STATIC
    src/json_stream.cpp
    src/parser.cpp
    src/tokenizer.cpp
    src/schema.cpp
)

target_include_directories(json PUBLIC include)

add_executable(json_cli
    src/main.cpp
)

target_link_libraries(json_cli PRIVATE json)

if(JSON_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
