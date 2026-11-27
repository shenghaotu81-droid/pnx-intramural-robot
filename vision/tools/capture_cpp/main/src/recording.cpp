#include "recording.hpp"
#include "capture_config.hpp"

#include "maix_video.hpp"

extern "C" {
#include <libavcodec/bsf.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

#include <condition_variable>
#include <cstring>
#include <deque>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <vector>

namespace capture {
namespace {

std::string capture_name(const char *kind) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto secs = floor<seconds>(now);
    const auto timestamp = system_clock::to_time_t(secs);
    std::tm local{};
    localtime_r(&timestamp, &local);
    std::ostringstream name;
    name << kind << '_' << std::put_time(&local, "%Y%m%d_%H%M%S") << '_' << std::setfill('0')
         << std::setw(6) << duration_cast<microseconds>(now - secs).count() << '_' << getpid();
    return name.str();
}

class OutputFile {
public:
    explicit OutputFile(std::filesystem::path path)
        : path_(std::move(path)), partial_(path_.parent_path() / ("." + path_.filename().string() + ".part")) {
        stream_.open(partial_, std::ios::binary);
        if (!stream_) {
            throw std::runtime_error("Cannot open " + partial_.string());
        }
        stream_.exceptions(std::ios::failbit | std::ios::badbit);
    }

    ~OutputFile() {
        std::error_code ignored;
        std::filesystem::remove(partial_, ignored);
    }

    const std::filesystem::path &path() const { return path_; }
    std::ostream &stream() { return stream_; }

    void write(const std::uint8_t *data, std::size_t size) {
        stream_.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(size));
    }

    void finish() {
        stream_.close();
        std::filesystem::rename(partial_, path_);
    }

private:
    std::filesystem::path path_;
    std::filesystem::path partial_;
    std::ofstream stream_;
};

void check_av(int result, const char *action) {
    if (result < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, error, sizeof(error));
        throw std::runtime_error(std::string(action) + ": " + error);
    }
}

// Annex B start codes use two or three leading zero bytes. Inspect the NAL
// header immediately after 00 00 01, including when preceded by another 00.
bool contains_sps(const std::vector<std::uint8_t> &packet) {
    for (std::size_t i = 3; i + 1 < packet.size(); ++i) {
        if (packet[i - 3] == 0 && packet[i - 2] == 0 && packet[i - 1] == 1 &&
            ((packet[i] >> 1) & 0x3f) == 33) {
            return true;
        }
    }
    return false;
}

class SpsCrop {
public:
    explicit SpsCrop(int crop_right) {
        const auto *filter = av_bsf_get_by_name("hevc_metadata");
        if (!filter) {
            throw std::runtime_error("MaixCDK FFmpeg is missing hevc_metadata");
        }
        AVBSFContext *context = nullptr;
        check_av(av_bsf_alloc(filter, &context), "Allocate HEVC filter");
        context_.reset(context);
        context_->par_in->codec_type = AVMEDIA_TYPE_VIDEO;
        context_->par_in->codec_id = AV_CODEC_ID_HEVC;
        context_->time_base_in = {1, fps};
        check_av(av_opt_set_int(context_->priv_data, "crop_right", crop_right, 0), "Set HEVC crop");
        check_av(av_bsf_init(context_.get()), "Initialize HEVC crop");
        packet_.reset(av_packet_alloc());
        if (!packet_) {
            throw std::runtime_error("Could not allocate HEVC packet");
        }
    }

    const AVPacket &apply(const std::vector<std::uint8_t> &packet) {
        if (packet.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("Oversized HEVC packet");
        }
        av_packet_unref(packet_.get());
        check_av(av_new_packet(packet_.get(), static_cast<int>(packet.size())), "Allocate packet payload");
        std::memcpy(packet_->data, packet.data(), packet.size());
        check_av(av_bsf_send_packet(context_.get(), packet_.get()), "Send HEVC SPS for crop");
        check_av(av_bsf_receive_packet(context_.get(), packet_.get()), "Receive cropped HEVC SPS");
        return *packet_;
    }

private:
    static void free_context(AVBSFContext *context) { av_bsf_free(&context); }
    static void free_packet(AVPacket *packet) { av_packet_free(&packet); }

    std::unique_ptr<AVBSFContext, decltype(&free_context)> context_{nullptr, free_context};
    std::unique_ptr<AVPacket, decltype(&free_packet)> packet_{nullptr, free_packet};
};

}  // namespace

// One outstanding VENC request; only owned compressed bytes may queue for disk.
struct EncodedFrame {
    FrameTiming timing;
    std::vector<std::uint8_t> packet;
    Clock::time_point enc_pop_start;
    std::int64_t enc_pop_us = 0, packet_us = 0, queue_wait_us = 0;
    std::size_t queue_depth = 0;
};

struct Recording::Impl {
    Impl(const std::filesystem::path &directory, int native_width)
        : name(capture_name("video")), video(directory / (name + ".h265")),
          timestamps(directory / (name + ".csv")), timing_log(directory / (name + ".timing.tsv")),
          encoder_width(native_width) {
        if (encoder_width != width && encoder_width != aligned_width) {
            throw std::runtime_error("Unexpected encoder input width");
        }
        if (encoder_width > width) {
            crop = std::make_unique<SpsCrop>(encoder_width - width);
        }
        encoder = std::make_unique<maix::video::Encoder>("", encoder_width, height,
            maix::image::FMT_YVU420SP, maix::video::VIDEO_H265_CBR, fps, fps, 6'000'000);
        timestamps.stream() << "frame_index,t_us,vpss_pts\n";
        timing_log.stream() << "frame_index\tpop_start_us\tt_us\tpop_us\tpop_timeouts\tretry_us"
            "\topen_us\twriter_wait_us\tpush_start_us\tpush_us\tpreview_us\tphoto_us"
            "\trelease_us\thold_us\tdisplay_count\tdisplay_us\tenc_pop_start_us\tenc_pop_us"
            "\tpacket_us\tcrop_us\tvideo_write_us\tcsv_write_us\toutput_end_us\tvpss_time_ref"
            "\tcpu_mapped\tpixel_map_us\tcamera_buffers\tqueue_wait_us\tqueue_depth\n";
        writer = std::thread(&Impl::write_loop, this);
        try {
            collector = std::thread(&Impl::collect_loop, this);
        } catch (...) {
            stop_worker();
            throw;
        }
    }

    ~Impl() { stop_worker(); }
    void collect_loop();
    void write_loop();
    void get_packet(EncodedFrame &frame);
    void write_frame(const EncodedFrame &frame, RecordStats &completed);
    void stop_worker();

    std::string name;
    OutputFile video;
    OutputFile timestamps;
    OutputFile timing_log;
    int encoder_width;
    std::unique_ptr<SpsCrop> crop;
    std::unique_ptr<maix::video::Encoder> encoder;
    static constexpr std::size_t queue_capacity = 8;
    std::deque<EncodedFrame> queue;
    Clock::time_point first{};
    RecordStats stats;
    std::mutex mutex;
    std::condition_variable changed;
    // Held until VENC data is copied, its stream released, and the packet queued.
    std::optional<FrameTiming> pending;
    std::exception_ptr failure;
    bool stopping = false;
    bool input_done = false;
    std::thread collector, writer;
};

Recording::Recording(const std::filesystem::path &directory, int native_width)
    : impl_(std::make_unique<Impl>(directory, native_width)) {}

Recording::~Recording() = default;

void Recording::append(Nv21Frame &frame, FrameTiming timing) {
    auto &rec = *impl_;
    if (static_cast<int>(frame.info().u32Width) != rec.encoder_width) {
        throw std::runtime_error("VPSS width changed during recording");
    }
    const auto wait_start = Clock::now();
    {
        std::unique_lock<std::mutex> lock(rec.mutex);
        rec.changed.wait(lock, [&] { return !rec.pending || rec.failure; });
        if (rec.failure) {
            std::rethrow_exception(rec.failure);
        }
    }
    timing.push_start = Clock::now();
    timing.writer_wait_us = micros(wait_start, timing.push_start);
    auto input = frame.encoder_frame();
    const auto result = rec.encoder->push(&input);
    const auto pushed = Clock::now();
    timing.push_us = micros(timing.push_start, pushed);
    if (result != maix::err::ERR_NONE) {
        throw std::runtime_error("VENC rejected the VPSS frame");
    }
    frame.release();
    timing.released = Clock::now();
    timing.release_us = micros(pushed, timing.released);
    {
        std::lock_guard<std::mutex> lock(rec.mutex);
        rec.pending = timing;
    }
    rec.changed.notify_all();
}

void Recording::Impl::collect_loop() {
    std::unique_lock<std::mutex> lock(mutex);
    try {
        for (;;) {
            changed.wait(lock, [&] { return pending || stopping; });
            if (!pending) {
                return;
            }
            EncodedFrame frame;
            frame.timing = *pending;
            lock.unlock();
            get_packet(frame);
            const auto queued_at = Clock::now();
            lock.lock();
            changed.wait(lock, [&] { return queue.size() < queue_capacity || failure; });
            if (!failure) {
                frame.queue_wait_us = micros(queued_at, Clock::now());
                frame.queue_depth = queue.size() + 1;
                queue.push_back(std::move(frame));
            }
            pending.reset();
            changed.notify_all();
        }
    } catch (...) {
        if (!lock.owns_lock()) lock.lock();
        failure = std::current_exception();
        pending.reset();
        changed.notify_all();
    }
}

void Recording::Impl::write_loop() {
    std::unique_lock<std::mutex> lock(mutex);
    try {
        for (;;) {
            changed.wait(lock, [&] { return !queue.empty() || input_done || failure; });
            if (queue.empty() || failure) {
                return;
            }
            auto frame = std::move(queue.front());
            queue.pop_front();
            auto completed = stats;
            changed.notify_all();
            lock.unlock();
            write_frame(frame, completed);
            lock.lock();
            stats = completed;
        }
    } catch (...) {
        if (!lock.owns_lock()) lock.lock();
        failure = std::current_exception();
        changed.notify_all();
    }
}

void Recording::Impl::stop_worker() {
    if (collector.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        changed.notify_all();
        collector.join();
    }
    if (writer.joinable()) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            input_done = true;
        }
        changed.notify_all();
        writer.join();
    }
}

void Recording::Impl::get_packet(EncodedFrame &frame) {
    frame.enc_pop_start = Clock::now();
    std::unique_ptr<maix::pipeline::Stream> encoded(encoder->pop());
    const auto encoded_at = Clock::now();
    frame.enc_pop_us = micros(frame.enc_pop_start, encoded_at);
    if (!encoded || encoded->data_count() <= 0) {
        throw std::runtime_error("VENC returned no HEVC access unit");
    }
    auto &packet = frame.packet;
    for (int i = 0; i < encoded->data_count(); ++i) {
        const std::unique_ptr<maix::Bytes> part(encoded->data(i));
        if (!part || !part->data || part->size() == 0) {
            throw std::runtime_error("VENC returned an empty HEVC packet segment");
        }
        packet.insert(packet.end(), part->data, part->data + part->size());
    }
    encoded.reset();
    frame.packet_us = micros(encoded_at, Clock::now());
}

void Recording::Impl::write_frame(const EncodedFrame &frame, RecordStats &completed) {
    const auto &timing = frame.timing;
    const auto &packet = frame.packet;
    const bool sps = (completed.frames == 0 || crop) && contains_sps(packet);
    if (completed.frames == 0) {
        if (!sps) {
            throw std::runtime_error("First HEVC access unit has no SPS");
        }
        first = timing.captured;
    }
    const auto assembled = Clock::now();
    const std::uint8_t *data = packet.data();
    auto size = packet.size();
    if (sps && crop) {
        const auto &filtered = crop->apply(packet);
        data = filtered.data;
        size = filtered.size;
    }
    const auto write_start = Clock::now();
    video.write(data, size);
    const auto video_done = Clock::now();
    const auto elapsed = micros(first, timing.captured);
    timestamps.stream() << completed.frames << ',' << elapsed << ',' << timing.driver_pts << '\n';
    const auto csv_done = Clock::now();

    timing_log.stream() << completed.frames << '\t' << micros(first, timing.pop_start) << '\t'
        << elapsed << '\t' << micros(timing.pop_start, timing.captured) << '\t' << timing.pop_timeouts
        << '\t' << timing.retry_us << '\t' << timing.open_us << '\t' << timing.writer_wait_us
        << '\t' << micros(first, timing.push_start) << '\t' << timing.push_us << '\t' << timing.preview_us
        << '\t' << timing.photo_us << '\t' << timing.release_us << '\t' << micros(timing.captured, timing.released)
        << '\t' << timing.display_count << '\t' << timing.display_us << '\t' << micros(first, frame.enc_pop_start)
        << '\t' << frame.enc_pop_us << '\t' << frame.packet_us
        << '\t' << (sps && crop ? micros(assembled, write_start) : 0) << '\t' << micros(write_start, video_done)
        << '\t' << micros(video_done, csv_done) << '\t' << micros(first, csv_done)
        << '\t' << timing.vpss_time_ref << '\t' << timing.cpu_mapped << '\t' << timing.pixel_map_us << '\t' << timing.camera_buffers
        << '\t' << frame.queue_wait_us << '\t' << frame.queue_depth << '\n';
    completed.elapsed_us = elapsed;
    ++completed.frames;
}

RecordStats Recording::stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

std::filesystem::path Recording::finish() {
    auto &rec = *impl_;
    rec.stop_worker();
    if (rec.failure) {
        std::rethrow_exception(rec.failure);
    }
    rec.encoder.reset();
    if (rec.stats.frames == 0) {
        return {};
    }
    rec.timestamps.finish();
    bool timing_published = false;
    try {
        rec.timing_log.finish();
        timing_published = true;
        rec.video.finish();
    } catch (...) {
        std::error_code ignored;
        if (timing_published) {
            std::filesystem::remove(rec.timing_log.path(), ignored);
        }
        std::filesystem::remove(rec.timestamps.path(), ignored);
        throw;
    }
    return rec.video.path();
}

std::filesystem::path save_jpeg(const std::filesystem::path &directory, const cv::Mat &nv21) {
    cv::Mat photo;
    cv::cvtColor(nv21, photo, cv::COLOR_YUV2BGR_NV21);
    std::vector<std::uint8_t> jpeg;
    if (!cv::imencode(".jpg", photo, jpeg, {cv::IMWRITE_JPEG_QUALITY, jpeg_quality})) {
        throw std::runtime_error("JPEG encode failed");
    }
    OutputFile output(directory / (capture_name("photo") + ".jpg"));
    output.write(jpeg.data(), jpeg.size());
    output.finish();
    return output.path();
}

}  // namespace capture
