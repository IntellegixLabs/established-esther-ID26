cmake_minimum_required(VERSION 3.16)
project(Established LANGUAGES CXX)

add_executable(esther esther.cpp)
target_compile_features(esther PRIVATE cxx_std_17)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(esther PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIX)
    enable_testing()
    add_test(
        NAME esther_smoke
        COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke.sh"
                "$<TARGET_FILE:esther>" "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()