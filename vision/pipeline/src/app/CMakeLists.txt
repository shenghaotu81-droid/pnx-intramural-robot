if(NOT VISION_BUILD_TOOLS AND NOT BUILD_TESTING)
    return()
endif()
# 离线应用调用 Pipeline，并负责读视频、导出、渲染和汇报。
add_library(vision_app offline/offline_runner.cpp rendering/debug_renderer.cpp
    export/video_exporter.cpp reporting/run_console.cpp reporting/run_report.cpp)
target_include_directories(vision_app PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${VISION_ROOT}/src/tracking")
target_link_libraries(vision_app PRIVATE vision_detection vision_pose vision_tracking)
target_link_libraries(vision_app PRIVATE vision_pipeline)
