#pragma once
#include "maix_pipeline.hpp"
#include <atomic>
#include <fstream>

namespace maix {
namespace err { enum Err { ERR_NONE }; }
namespace image { enum Format { FMT_YVU420SP }; }
namespace video {
enum VideoType { VIDEO_H265_CBR };
inline std::ifstream input, lengths;
inline std::atomic<unsigned> released = 0;

class Encoder {
public:
    Encoder(std::string, int width, int height, image::Format, VideoType, int fps, int gop, int bitrate)
        : width_(width) {
        assert(height == 1080 && fps == 30 && gop == 30 && bitrate == 6'000'000);
    }
    err::Err push(pipeline::Frame *frame) {
        assert(!pending_ && frame->info.stVFrame.u32Width == width_);
        pending_ = true;
        sequence_ = frame->info.stVFrame.u32TimeRef;
        return err::ERR_NONE;
    }
    pipeline::Stream *pop() {
        assert(pending_ && released >= sequence_);
        pending_ = false;
        std::size_t length;
        if (!(lengths >> length)) return nullptr;
        std::vector<std::uint8_t> bytes(length);
        assert(length > 2 && input.read(reinterpret_cast<char *>(bytes.data()), length));
        return new pipeline::Stream(std::move(bytes));
    }
private:
    unsigned width_, sequence_ = 0;
    bool pending_ = false;
};
}  // namespace video
}  // namespace maix
