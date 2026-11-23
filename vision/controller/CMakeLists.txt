cmake_minimum_required(VERSION 3.16)
project(controller LANGUAGES CXX)

# 通用控制器库
add_library(controller src/controller.cpp src/yaw_filter.cpp)
target_include_directories(controller PUBLIC include)
target_compile_features(controller PUBLIC cxx_std_17)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
