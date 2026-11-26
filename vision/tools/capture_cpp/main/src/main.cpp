#include "capture_config.hpp"
#include "nv21_frame.hpp"
#include "preview_ui.hpp"
#include "recording.hpp"

#include "maix_basic.hpp"
#include "maix_camera.hpp"

#include <atomic>
#include <future>
#include <mutex>
#include <stdexcept>
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

    PreviewState snapshot() {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    void preview(cv::Mat rgb, const RecordStats &stats) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.rgb = std::move(rgb);
        state_.stats = stats;
    }

    void set_recording(bool active) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.recording = active;
        state_.stats = {};
    }

    void notice(std::string text) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.notice = std::move(text);
    }

    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};

private:
    std::mutex mutex_;
    std::vector<Action> actions_;
    PreviewState state_;
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

void capture_frames(maix::camera::Camera &camera, cv::Size preview_size, Exchange &exchange) {
    std::unique_ptr<Recording> recording;
    std::future<std::filesystem::path> photo;
    bool record_requested = false;
    bool photo_requested = false;
    auto next_preview = Clock::time_point{};
    int warmup_remaining = warmup_frames;
    int timeouts = 0;
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

            std::unique_ptr<maix::pipeline::Frame> native(camera.pop(camera_timeout_ms));
            const auto captured = Clock::now();
            if (!native) {
                if (++timeouts >= max_camera_timeouts) {
                    throw std::runtime_error("Repeated camera read timeouts");
                }
                continue;
            }
            timeouts = 0;
            if (warmup_remaining > 0) {
                if (--warmup_remaining == 0) {
                    maix::log::info("VPSS frame %dx%d, NV21 strides %d/%d", native->width(), native->height(),
                                   native->stride(0), native->stride(1));
                    exchange.notice("Ready");
                }
                continue;
            }

            const Nv21Frame frame(*native);
            if (record_requested) {
                try {
                    if (!recording) {
                        recording = std::make_unique<Recording>(output_directory, frame.encoder_width());
                        exchange.set_recording(true);
                        exchange.notice("");
                    }
                    recording->append(*native, captured, frame.driver_pts());
                } catch (const std::exception &error) {
                    maix::log::error("Recording failed: %s", error.what());
                    recording.reset();
                    record_requested = false;
                    exchange.set_recording(false);
                    exchange.notice("Recording failed");
                }
            }

            if (photo_requested) {
                photo_requested = false;
                try {
                    photo = std::async(std::launch::async, [pixels = frame.copy_pixels()] {
                        return save_jpeg(output_directory, pixels);
                    });
                    exchange.notice("Saving photo");
                } catch (const std::exception &error) {
                    maix::log::error("Photo capture failed: %s", error.what());
                    exchange.notice("Photo capture failed");
                }
            }

            if (captured >= next_preview) {
                exchange.preview(frame.preview_rgb(preview_size), recording ? recording->stats() : RecordStats{});
                next_preview = captured + (recording ? record_preview_interval : idle_preview_interval);
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
    maix::camera::Camera camera(width, height, maix::image::FMT_YVU420SP,
                               nullptr, fps, camera_buffers, true, false);
    maix::display::Display display;
    maix::touchscreen::TouchScreen touch;
    PreviewUi ui(display);
    Exchange exchange;
    std::thread worker(capture_frames, std::ref(camera), ui.preview_size(), std::ref(exchange));

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
                const auto state = exchange.snapshot();
                ui.show(state);
                next_draw = now + (state.recording ? record_preview_interval : idle_preview_interval);
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
