list(APPEND ADD_INCLUDE "include")
list(APPEND ADD_SRCS
    "src/main.cpp"
    "src/nv21_frame.cpp"
    "src/recording.cpp"
    "src/preview_ui.cpp"
)
list(APPEND ADD_REQUIREMENTS vision opencv FFmpeg sophgo-middleware)
register_component()
target_compile_features(main PRIVATE cxx_std_17)
