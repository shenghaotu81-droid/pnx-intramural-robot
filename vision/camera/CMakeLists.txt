cmake_minimum_required(VERSION 3.16)
project(camera LANGUAGES CXX)

find_package(OpenCV REQUIRED COMPONENTS core)

# 相机库：MaixCDK 相机转管线帧（BGR Mat + frame_id + timestamp_us）
add_library(camera src/camera.cpp src/config_loader.cpp)
target_include_directories(camera PUBLIC include)
target_compile_features(camera PUBLIC cxx_std_17)
target_link_libraries(camera PRIVATE opencv_core)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    include(CTest)

    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
