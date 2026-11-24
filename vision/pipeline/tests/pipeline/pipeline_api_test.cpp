#include "vision/pipeline.hpp"
#include "assertions.hpp"
#include <iostream>
#include <opencv2/videoio.hpp>

namespace
{
    using test_support::check;

    // tag36h11 ID 0 的八格图案（含黑边），外围留白。消费者只链接 vision_pipeline。
    cv::Mat tag_image(cv::Size size)
    {
        const char *rows[]{"00000000", "01101010", "00111010", "00110000",
                           "01010000", "00101100", "00001000", "00000000"};
        cv::Mat image(size, CV_8UC3, cv::Scalar::all(255));
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x)
                if (rows[y][x] == '0')
                    image(cv::Rect(80 + 20 * x, 80 + 20 * y, 20, 20)).setTo(cv::Scalar::all(0));
        return image;
    }

    mark::PipelineOutput process(mark::Pipeline &pipeline, const cv::Mat &image, uint64_t id)
    {
        const auto original = image.clone();
        const auto us = static_cast<int64_t>(id) * 10000;
        auto out = pipeline.process({image, id, us});
        const auto &raw = pipeline.last_detection();
        check(raw.frame_id_ == id && raw.timestamp_us_ == us, "stale detection");
        check(out.tracking.input_accepted_ && out.tracking.input_stamp_.frame_id_ == id &&
              out.tracking.input_stamp_.received_us_ == us, "tracking lost frame stamp");
        for (const auto &p : out.poses)
            check(p.stamp_.frame_id_ == id && p.stamp_.received_us_ == us, "pose lost frame stamp");
        check(cv::norm(image, original, cv::NORM_INF) == 0, "input image changed");
        return out;
    }

    void sequence(mark::Pipeline &pipeline, const cv::Mat &mark_image)
    {
        auto first = process(pipeline, mark_image, 10);
        check(first.poses.size() == 1 && first.poses[0].estimate_ &&
              first.poses[0].source_ == "MARK", "first MARK missing");
        check(first.tracking.events_ == std::vector<std::string>{"TRACK_CREATED"} &&
              first.tracking.tracks_.size() == 1, "track creation missing");

        auto next = process(pipeline, mark_image, 11);
        check(next.tracking.association_nis_.has_value() && next.tracking.tracks_.size() == 1 &&
              next.tracking.tracks_[0].velocity_mps_.has_value(), "second measurement lost");

        cv::Mat blank = cv::Mat::zeros(mark_image.size(), mark_image.type());
        auto coast = process(pipeline, blank, 12);
        check(pipeline.last_detection().detections_.empty() && coast.poses.size() == 1 &&
              coast.poses[0].source_ == "MARK" && !coast.poses[0].estimate_, "empty MARK missing");
        check(coast.tracking.tracks_.size() == 1 &&
              coast.tracking.tracks_[0].state_ == mark::tracking::TrackState::Coasting &&
              coast.tracking.tracks_[0].last_measurement_stamp_.frame_id_ == 11, "coast missing");
    }

    void tag_no_profile(mark::Pipeline &pipeline, const cv::Mat &mark_image)
    {
        auto mark = process(pipeline, mark_image, 20);
        check(mark.poses.size() == 1 && mark.poses[0].estimate_ &&
              mark.tracking.tracks_.empty(), "empty profile should keep MARK pose only");

        auto out = process(pipeline, tag_image(mark_image.size()), 21);
        check(out.poses.size() == 2 && out.poses[0].source_ == "MARK" &&
              !out.poses[0].estimate_, "expected empty MARK before tag");
        const auto &tag = out.poses[1];
        check(tag.source_ == "APRILTAG" && !tag.estimate_ &&
              tag.reason_ == "METRIC_SIZE_MISSING_OR_INVALID", "nonmetric tag missing");
        check(out.tracking.tracks_.empty(), "empty profile produced track");
    }
}

int main(int argc, char **argv)
{
    try
    {
        check(argc == 3 || argc == 4, "expected CONFIG_DIR VIDEO [tag]");
        mark::Pipeline pipeline(argv[1]);
        cv::VideoCapture video(argv[2]);
        cv::Mat image;
        check(video.read(image), "fixture video unreadable");
        check(pipeline.pose_config().camera_.image_size_ == image.size(), "camera config lost");
        if (argc == 4)
            tag_no_profile(pipeline, image);
        else
            sequence(pipeline, image);
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
