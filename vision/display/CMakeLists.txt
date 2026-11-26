# display.cpp + render.cpp，find_package + tests 开关
# vision/display: 调试预览库（叠加绘制 + 上屏）。
# 只编给板子用（交叉编译），笔记本上不编整个库（display.cpp 依赖 MaixCDK）；
# 笔记本上只测 render.cpp（纯 OpenCV），见 tests/CMakeLists.txt。
# 无配置文件，绘制参数（颜色、字号、布局）硬编码在 render.cpp 里。
cmake_minimum_required(VERSION 3.16)
project(display LANGUAGES CXX)

find_package(OpenCV REQUIRED COMPONENTS core imgproc)

# display.cpp（MaixCDK 上屏）+ render.cpp（OpenCV 绘制）。
# 板上由顶层通过 ADD_REQUIREMENTS 提供 opencv 时，上面这行 find_package
# 在移植时跳过（与 camera/adapter/controller 的处理一致）。
add_library(display
    src/display.cpp
    src/render.cpp
)

target_include_directories(display
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_compile_features(display PUBLIC cxx_std_17)

target_link_libraries(display
    PUBLIC
    opencv_core
    opencv_imgproc
)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    include(CTest)

    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
