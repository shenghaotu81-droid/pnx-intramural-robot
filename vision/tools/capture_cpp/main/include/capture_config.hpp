#pragma once

namespace capture {

inline constexpr int width = 1440;
inline constexpr int height = 1080;
inline constexpr int fps = 30;
inline constexpr int aligned_width = 1472;
inline constexpr int camera_buffers = 1;
inline constexpr int warmup_frames = 10;
inline constexpr int camera_timeout_ms = 150;
// At 30 fps a 432 ms write stall can deliver 13 new compressed frames.
inline constexpr int packet_queue_capacity = 16;
inline constexpr int jpeg_quality = 95;
inline constexpr char output_directory[] = "/root/capture_cpp";

}  // namespace capture
