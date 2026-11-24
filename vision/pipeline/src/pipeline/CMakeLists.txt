# 按帧调用的管线：串联检测、位姿、跟踪，板上和离线共用。
add_library(vision_pipeline pipeline.cpp)
target_include_directories(vision_pipeline PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(vision_pipeline PUBLIC vision_detection vision_pose vision_tracking
    vision_config)
target_compile_options(vision_pipeline PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>)
