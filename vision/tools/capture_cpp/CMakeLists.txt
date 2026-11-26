cmake_minimum_required(VERSION 3.13)

set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)
include(${SDK_PATH}/tools/cmake/compile.cmake)
project(${PROJECT_ID})

if(NOT PLATFORM_MAIXCAM)
    message(FATAL_ERROR "capture_cpp requires MaixCAM or MaixCAM-Pro (-p maixcam)")
endif()
