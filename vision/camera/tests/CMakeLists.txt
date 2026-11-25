# 配置加载器测试：只编 config_loader.cpp（OpenCV），不编 camera.cpp（MaixCDK，编给板子用的部分）。
add_executable(camera_config_load_test config_load_test.cpp ../src/config_loader.cpp)
target_include_directories(camera_config_load_test PRIVATE ../include)
target_compile_features(camera_config_load_test PRIVATE cxx_std_17)
target_link_libraries(camera_config_load_test PRIVATE opencv_core)

add_test(NAME camera_config_load_test COMMAND camera_config_load_test
         WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
