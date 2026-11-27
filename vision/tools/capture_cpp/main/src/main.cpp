#include "capture_config.hpp"
#include "nv21_frame.hpp"
#include "preview_ui.hpp"
#include "recording.hpp"

#include "maix_basic.hpp"
#include "maix_camera.hpp"

#include <atomic>
#include <cstdlib>
#include <future>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace capture {
namespace {

using namespace std::chrono_literals;
constexpr auto idle_preview_interval = 66ms;
constexpr auto record_preview_interval = 125ms;
constexpr int max_camera_timeouts = 5;

class Exchange {
public:
    void request(Action action) {
        std::lock_guard<std::mutex> lock(mutex_);
        actions_.push_back(action);
    }

    std::vector<Action> take_actions() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::exchange(actions_, {});
    }

    std::optional<PreviewState> take_redraw() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dirty_) {
            return {};
        }
        dirty_ = false;
        return state_;
    }

    void display_finished(std::int64_t duration) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++draws_;
        draw_us_ += duration;
    }

    void take_display_times(FrameTiming &timing) {
        std::lock_guard<std::mutex> lock(mutex_);
        timing.display_count = std::exchange(draws_, 0);
        timing.display_us = std::exchange(draw_us_, 0);
    }

    void preview(cv::Mat nv21, const RecordStats &stats) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.nv21 = std::move(nv21);
        state_.stats = stats;
        dirty_ = true;
    }

    void set_recording(bool active) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.recording = active;
        state_.stats = {};
        dirty_ = true;
    }

    void notice(std::string text) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.notice = std::move(text);
        dirty_ = true;
    }

    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};

private:
    std::mutex mutex_;
    std::vector<Action> actions_;
    PreviewState state_;
    bool dirty_ = true;
    unsigned draws_ = 0;
    std::int64_t draw_us_ = 0;
};

void close_video(std::unique_ptr<Recording> &recording, Exchange &exchange) {
    exchange.set_recording(false);
    if (!recording) {
        return;
    }
    auto completed = std::move(recording);
    try {
        const auto path = completed->finish();
        if (path.empty()) {
            exchange.notice("No video frames");
        } else {
            const auto &stats = completed->stats();
            maix::log::info("Saved %s, %zu frames, %.3f s, %.2f fps", path.c_str(),
                stats.frames, stats.elapsed_us / 1'000'000.0, stats.actual_fps());
            exchange.notice("Video saved");
        }
    } catch (const std::exception &error) {
        maix::log::error("Could not save video: %s", error.what());
        exchange.notice("Video save failed");
    }
}

void close_photo(std::future<std::filesystem::path> &photo, Exchange &exchange) {
    try {
        maix::log::info("Saved %s", photo.get().c_str());
        exchange.notice("Photo saved");
    } catch (const std::exception &error) {
        maix::log::error("Could not save photo: %s", error.what());
        exchange.notice("Photo save failed");
    }
}

void capture_frames(maix::camera::Camera &camera, int buffers, cv::Size preview_size, Exchange &exchange) {
    std::unique_ptr<Recording> recording;
    std::future<std::filesystem::path> photo;
    bool record_requested = false;
    bool photo_requested = false;
    auto next_preview = Clock::time_point{};
    int warmup_remaining = warmup_frames;
    const int channel = camera.get_channel();
    const cv::Size record_preview = preview_size.width > 240 ? cv::Size(240, 180) : preview_size;
    int native_width = 0;
    std::int64_t encoder_open_us = 0;
    int timeouts = 0;
    std::int64_t retry_us = 0;
    exchange.notice("Warming up");

    try {
        while (!exchange.stop) {
            if (photo.valid() && photo.wait_for(0ms) == std::future_status::ready) {
                close_photo(photo, exchange);
            }
            for (const auto action : exchange.take_actions()) {
                if (action == Action::photo) {
                    if (photo.valid()) {
                        exchange.notice("Photo still saving");
                    } else {
                        photo_requested = true;
                    }
                } else if (action == Action::record) {
                    record_requested = !record_requested;
                    if (!record_requested) {
                        close_video(recording, exchange);
                    }
                }
            }

            if (record_requested && !recording && warmup_remaining == 0) {
                try {
                    const auto start = Clock::now();
                    recording = std::make_unique<Recording>(output_directory, native_width);
                    encoder_open_us = micros(start, Clock::now());
                    exchange.set_recording(true);
                    exchange.notice("");
                } catch (const std::exception &error) {
                    maix::log::error("Could not open recording: %s", error.what());
                    record_requested = false;
                    exchange.notice("Recording failed");
                }
            }

            FrameTiming timing{};
            timing.camera_buffers = buffers;
            Nv21Frame frame(channel);
            timing.pop_start = Clock::now();
            const int status = frame.acquire(camera_timeout_ms);
            timing.captured = Clock::now();
            if (status != 0) {
                retry_us += micros(timing.pop_start, timing.captured);
                if (++timeouts >= max_camera_timeouts) {
                    maix::log::error("%d failed VPSS reads, %lld us in pop, last status %d",
                        timeouts, static_cast<long long>(retry_us), status);
                    throw std::runtime_error("Repeated camera read timeouts");
                }
                continue;
            }
            timing.pop_timeouts = std::exchange(timeouts, 0);
            timing.retry_us = std::exchange(retry_us, 0);
            exchange.take_display_times(timing);
            frame.validate();
            const auto &info = frame.info();
            if (warmup_remaining > 0) {
                if (--warmup_remaining == 0) {
                    native_width = info.u32Width;
                    maix::log::info("VPSS frame %ux%u, NV21 strides %u/%u", info.u32Width, info.u32Height,
                                   info.u32Stride[0], info.u32Stride[1]);
                    exchange.notice("Ready");
                }
                continue;
            }

            const bool preview_due = timing.captured >= next_preview;
            if (photo_requested || preview_due) {
                const auto start = Clock::now();
                frame.map_pixels();
                timing.cpu_mapped = true;
                timing.pixel_map_us = micros(start, Clock::now());
            }

            if (photo_requested) {
                const auto start = Clock::now();
                photo_requested = false;
                try {
                    photo = std::async(std::launch::async, [pixels = frame.sample({width, height})] {
                        return save_jpeg(output_directory, pixels);
                    });
                    exchange.notice("Saving photo");
                } catch (const std::exception &error) {
                    maix::log::error("Photo capture failed: %s", error.what());
                    exchange.notice("Photo capture failed");
                }
                timing.photo_us = micros(start, Clock::now());
            }

            if (preview_due) {
                const auto start = Clock::now();
                exchange.preview(frame.sample(recording ? record_preview : preview_size), recording ? recording->stats() : RecordStats{});
                next_preview = timing.captured + (recording ? record_preview_interval : idle_preview_interval);
                timing.preview_us = micros(start, Clock::now());
            }
            if (recording) {
                timing.open_us = std::exchange(encoder_open_us, 0);
                timing.driver_pts = info.u64PTS;
                timing.vpss_time_ref = info.u32TimeRef;
                try {
                    recording->append(frame, timing);
                } catch (const std::exception &error) {
                    maix::log::error("Recording failed: %s", error.what());
                    recording.reset();
                    record_requested = false;
                    exchange.set_recording(false);
                    exchange.notice("Recording failed");
                }
            }
        }
    } catch (const std::exception &error) {
        maix::log::error("Capture stopped: %s", error.what());
        exchange.failed = true;
    }

    close_video(recording, exchange);
    if (photo.valid()) {
        close_photo(photo, exchange);
    }
    if (exchange.failed) {
        exchange.notice("Capture stopped - check log");
    }
}

int run() {
    std::filesystem::create_directories(output_directory);
    cv::setNumThreads(1);
    int buffers = camera_buffers;
    if (const char *value = std::getenv("CAPTURE_CPP_CAMERA_BUFFERS")) {
        if (std::string_view(value) == "2") {
            buffers = 2;
        } else if (std::string_view(value) != "1") {
            throw std::invalid_argument("CAPTURE_CPP_CAMERA_BUFFERS must be 1 or 2");
        }
    }
    maix::log::info("Camera requested at %d fps with %d buffer(s)", fps, buffers);
    maix::camera::Camera camera(width, height, maix::image::FMT_YVU420SP,
                               nullptr, fps, buffers, true, false);
    maix::display::Display display;
    maix::touchscreen::TouchScreen touch;
    PreviewUi ui(display);
    Exchange exchange;
    std::thread worker(capture_frames, std::ref(camera), buffers, ui.preview_size(), std::ref(exchange));

    try {
        auto next_draw = Clock::time_point{};
        while (!maix::app::need_exit()) {
            const auto action = ui.read_touch(touch);
            if (action == Action::exit) {
                break;
            }
            if (action != Action::none && !exchange.failed) {
                exchange.request(action);
            }
            const auto now = Clock::now();
            if (now >= next_draw) {
                if (const auto state = exchange.take_redraw()) {
                    const auto draw_start = Clock::now();
                    ui.show(*state);
                    exchange.display_finished(micros(draw_start, Clock::now()));
                    next_draw = now + (state->recording ? record_preview_interval : idle_preview_interval);
                }
            }
            std::this_thread::sleep_for(10ms);
        }
    } catch (...) {
        exchange.stop = true;
        worker.join();
        throw;
    }
    exchange.stop = true;
    worker.join();
    return exchange.failed ? 1 : 0;
}

}  // namespace
}  // namespace capture

int main() {
    maix::sys::register_default_signal_handle();
    try {
        return capture::run();
    } catch (const std::exception &error) {
        maix::log::error("Capture app: %s", error.what());
        return 1;
    }
}
