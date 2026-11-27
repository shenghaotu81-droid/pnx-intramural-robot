// Use real middleware types. Only the hardware calls and the encoded packet source are replaced.
#include "capture_config.hpp"
#include "recording.hpp"
#include "maix_video.hpp"
#include <cvi_sys.h>
#include <cvi_vpss.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <sstream>

using namespace capture;
namespace {
VIDEO_FRAME_INFO_S next_frame{};
bool held = false, mapped = false, fail_map = false;
unsigned map_calls = 0;
std::vector<std::uint8_t> pixels(1472 * 1080 * 3 / 2, 0xff);
}

extern "C" CVI_S32 CVI_VPSS_GetChnFrame(VPSS_GRP group, VPSS_CHN ch, VIDEO_FRAME_INFO_S *out, CVI_S32 ms) {
    assert(group == 0 && ch == 1 && !held);
    if (!ms) return -1;
    held = true;
    *out = next_frame;
    return CVI_SUCCESS;
}
extern "C" CVI_S32 CVI_VPSS_ReleaseChnFrame(VPSS_GRP group, VPSS_CHN ch, const VIDEO_FRAME_INFO_S *frame) {
    assert(group == 0 && ch == 1 && held && !mapped);
    maix::video::released = frame->stVFrame.u32TimeRef;
    held = false;
    return CVI_SUCCESS;
}
extern "C" void *CVI_SYS_MmapCache(CVI_U64 addr, CVI_U32 len) {
    assert(held && !mapped && len == pixels.size() && addr == next_frame.stVFrame.u64PhyAddr[0]);
    if (fail_map) return nullptr;
    ++map_calls;
    mapped = true;
    return pixels.data();
}
extern "C" CVI_S32 CVI_SYS_Munmap(void *addr, CVI_U32 len) {
    assert(held && mapped && addr == pixels.data() && len == pixels.size());
    mapped = false;
    return CVI_SUCCESS;
}

int main(int argc, char **argv) {
    assert(argc == 6);
    const int native_width = std::stoi(argv[5]);
    auto &f = next_frame.stVFrame;
    f.u32Width = native_width;
    f.u32Height = height;
    f.enPixelFormat = PIXEL_FORMAT_NV21;
    f.u32Stride[0] = f.u32Stride[1] = 1472;
    f.u32Length[0] = 1472 * height;
    f.u32Length[1] = f.u32Length[0] / 2;
    f.u64PhyAddr[0] = 0x10000000;
    f.u64PhyAddr[1] = f.u64PhyAddr[0] + f.u32Length[0];
    Nv21Frame empty(1);
    assert(empty.acquire(0) != CVI_SUCCESS);
    for (bool invalid_layout : {false, true}) {
        try {
            Nv21Frame frame(1);
            if (invalid_layout) ++f.u64PhyAddr[1];
            else fail_map = true;
            assert(frame.acquire(150) == CVI_SUCCESS);
            frame.validate();
            frame.map_pixels();
            assert(false);
        } catch (const std::runtime_error &) {
            assert(!held && !mapped);
        }
        if (invalid_layout) --f.u64PhyAddr[1];
        fail_map = false;
    }
    // Red NV21 with distinct padding. The copied preview and JPG must survive native release.
    for (int y = 0; y < height; ++y) std::fill_n(pixels.data() + 1472*y, width, 81);
    for (int y = 0; y < height/2; ++y) {
        auto *row = pixels.data() + f.u32Length[0] + 1472*y;
        for (int x = 0; x < width; x += 2) { row[x] = 240; row[x+1] = 90; }
    }
    cv::Mat preview, photo;
    {
        Nv21Frame frame(1);
        assert(frame.acquire(150) == 0);
        frame.validate();
        frame.map_pixels();
        preview = frame.sample({240, 180});
        frame.map_pixels();
        photo = frame.sample({width, height});
    }
    assert(map_calls == 1 && !held && !mapped);
    std::fill(pixels.begin(), pixels.end(), 0);
    cv::Mat rgb;
    cv::cvtColor(preview, rgb, cv::COLOR_YUV2RGB_NV21);
    assert(rgb.size() == cv::Size(240, 180) && rgb.at<cv::Vec3b>(179,239)[0] > 245);
    const auto jpg = cv::imread(save_jpeg(argv[4], photo).string());
    assert(jpg.size() == cv::Size(width,height) && jpg.at<cv::Vec3b>(1079,1439)[2] > 245);

    maix::video::input.open(argv[1], std::ios::binary);
    maix::video::lengths.open(argv[2]);
    std::ifstream csv(argv[3]);
    std::string line;
    std::getline(csv, line);
    assert(line == "frame_index,t_us,vpss_pts");
    const auto origin = Clock::now();
    unsigned count = 0;
    try {
        Recording rec(argv[4], native_width);
        while (std::getline(csv, line)) {
            std::istringstream row(line);
            unsigned index;
            std::int64_t elapsed;
            char comma;
            row >> index >> comma >> elapsed >> comma >> f.u64PTS;
            assert(row && index == count++);
            f.u32TimeRef = count;
            Nv21Frame frame(1);
            assert(frame.acquire(150) == 0);
            frame.validate();
            FrameTiming t{};
            t.driver_pts = f.u64PTS;
            t.vpss_time_ref = count;
            t.pop_start = t.captured = origin + std::chrono::microseconds(elapsed);
            rec.append(frame, t);
        }
        rec.finish();
        assert(rec.stats().frames == count && map_calls == 1);
    } catch (const std::runtime_error &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
