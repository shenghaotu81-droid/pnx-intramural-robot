#include "recording.hpp"
#include "capture_config.hpp"

#include "maix_video.hpp"

extern "C" {
#include <libavcodec/bsf.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <limits>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <sstream>
#include <stdexcept>
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

    void write(const std::vector<std::uint8_t> &packet, OutputFile &output) {
        if (packet.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("Oversized HEVC packet");
        }
        check_av(av_new_packet(packet_.get(), static_cast<int>(packet.size())), "Allocate packet payload");
        std::memcpy(packet_->data, packet.data(), packet.size());
        check_av(av_bsf_send_packet(context_.get(), packet_.get()), "Send HEVC SPS for crop");
        check_av(av_bsf_receive_packet(context_.get(), packet_.get()), "Receive cropped HEVC SPS");
        output.write(packet_->data, packet_->size);
        av_packet_unref(packet_.get());
    }

private:
    static void free_context(AVBSFContext *context) { av_bsf_free(&context); }
    static void free_packet(AVPacket *packet) { av_packet_free(&packet); }

    std::unique_ptr<AVBSFContext, decltype(&free_context)> context_{nullptr, free_context};
    std::unique_ptr<AVPacket, decltype(&free_packet)> packet_{nullptr, free_packet};
};

}  // namespace

struct Recording::Impl {
    Impl(const std::filesystem::path &directory, int native_width)
        : name(capture_name("video")), video(directory / (name + ".h265")),
          timestamps(directory / (name + ".csv")), encoder_width(native_width) {
        if (encoder_width != width && encoder_width != aligned_width) {
            throw std::runtime_error("Unexpected encoder input width");
        }
        if (encoder_width > width) {
            crop = std::make_unique<SpsCrop>(encoder_width - width);
        }
        encoder = std::make_unique<maix::video::Encoder>("", encoder_width, height,
            maix::image::FMT_YVU420SP, maix::video::VIDEO_H265_CBR, fps, fps, 6'000'000);
        timestamps.stream() << "frame_index,t_us,vpss_pts\n";
    }

    std::string name;
    OutputFile video;
    OutputFile timestamps;
    int encoder_width;
    std::unique_ptr<SpsCrop> crop;
    std::unique_ptr<maix::video::Encoder> encoder;
    std::vector<std::uint8_t> packet;
    Clock::time_point first{};
    RecordStats stats;
};

Recording::Recording(const std::filesystem::path &directory, int native_width)
    : impl_(std::make_unique<Impl>(directory, native_width)) {}

Recording::~Recording() = default;

void Recording::append(maix::pipeline::Frame &frame, Clock::time_point captured, std::uint64_t driver_pts) {
    auto &rec = *impl_;
    if (frame.width() != rec.encoder_width) {
        throw std::runtime_error("VPSS width changed during recording");
    }
    if (rec.encoder->push(&frame) != maix::err::ERR_NONE) {
        throw std::runtime_error("VENC rejected the VPSS frame");
    }
    std::unique_ptr<maix::pipeline::Stream> encoded(rec.encoder->pop());
    if (!encoded || encoded->data_count() <= 0) {
        throw std::runtime_error("VENC returned no HEVC access unit");
    }
    rec.packet.clear();
    for (int i = 0; i < encoded->data_count(); ++i) {
        const std::unique_ptr<maix::Bytes> part(encoded->data(i));
        if (!part || !part->data || part->size() == 0) {
            throw std::runtime_error("VENC returned an empty HEVC packet segment");
        }
        rec.packet.insert(rec.packet.end(), part->data, part->data + part->size());
    }
    encoded.reset();

    const bool sps = contains_sps(rec.packet);
    if (rec.stats.frames == 0) {
        if (!sps) {
            throw std::runtime_error("First HEVC access unit has no SPS");
        }
        rec.first = captured;
    }
    if (sps && rec.crop) {
        rec.crop->write(rec.packet, rec.video);
    } else {
        rec.video.write(rec.packet.data(), rec.packet.size());
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(captured - rec.first).count();
    rec.timestamps.stream() << rec.stats.frames << ',' << elapsed << ',' << driver_pts << '\n';
    rec.stats.elapsed_us = elapsed;
    ++rec.stats.frames;
}

const RecordStats &Recording::stats() const { return impl_->stats; }

std::filesystem::path Recording::finish() {
    auto &rec = *impl_;
    rec.encoder.reset();
    if (rec.stats.frames == 0) {
        return {};
    }
    rec.timestamps.finish();
    try {
        rec.video.finish();
    } catch (...) {
        std::error_code ignored;
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
