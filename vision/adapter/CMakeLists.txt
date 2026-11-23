cmake_minimum_required(VERSION 3.16)
project(adapter)

add_library(adapter src/adapter.cpp)
target_include_directories(adapter PUBLIC include)
target_compile_features(adapter PUBLIC cxx_std_17)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
