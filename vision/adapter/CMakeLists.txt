cmake_minimum_required(VERSION 3.16)
project(adapter LANGUAGES CXX)

find_package(OpenCV REQUIRED)

# 独立构建时补 controller；vision 层构建时 controller 已存在
if(NOT TARGET controller)
    add_subdirectory(../controller controller)
endif()

# 适配层库
add_library(adapter src/adapter.cpp src/config_loader.cpp)
target_include_directories(adapter PUBLIC include)
target_compile_features(adapter PUBLIC cxx_std_17)
target_include_directories(adapter PUBLIC ${OpenCV_INCLUDE_DIRS})
target_link_libraries(adapter PUBLIC controller ${OpenCV_LIBS})

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
