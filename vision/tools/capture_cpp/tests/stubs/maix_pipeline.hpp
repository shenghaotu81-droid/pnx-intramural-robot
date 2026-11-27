// Host replay substitutes for the Maix objects that carry a descriptor or compressed bytes.
#pragma once
#include <cassert>
#include <cstdint>
#include <linux/cvi_comm_video.h>
#include <string>
#include <vector>

namespace maix {
struct Bytes {
    std::uint8_t *data;
    std::size_t length;
    std::size_t size() const { return length; }
};

namespace pipeline {
class Frame {
public:
    Frame(void *frame, bool auto_delete, const std::string &from)
        : info(*static_cast<VIDEO_FRAME_INFO_S *>(frame)) {
        assert(!auto_delete && from == "vpsschn,0,1");
    }
    VIDEO_FRAME_INFO_S info;
};

class Stream {
public:
    explicit Stream(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    int data_count() const { return 2; }
    Bytes *data(int i) {
        // Split the Annex B prefix across segments.
        if (i == 0) return new Bytes{bytes_.data(), 2};
        return new Bytes{bytes_.data() + 2, bytes_.size() - 2};
    }

private:
    std::vector<std::uint8_t> bytes_;
};
}  // namespace pipeline
}  // namespace maix
