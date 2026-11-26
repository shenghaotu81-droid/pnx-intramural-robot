# render 测试：只编 render.cpp（纯 OpenCV），不编 display.cpp（MaixCDK，编给板子用的部分）。
# 与 camera 的 config_loader 测试同一个套路：测试可执行文件直接把能编的那份实现编进来。
add_executable(display_render_test render_test.cpp ../src/render.cpp)
target_include_directories(display_render_test PRIVATE ../include)
target_compile_features(display_render_test PRIVATE cxx_std_17)
target_link_libraries(display_render_test PRIVATE opencv_core opencv_imgproc)

add_test(NAME display_render_test COMMAND display_render_test
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
