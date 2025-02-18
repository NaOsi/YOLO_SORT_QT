#include "cthread_video.h"
#include <random>

Cthread_video::Cthread_video(QThread *parent) : QThread(parent)
{
}

void Cthread_video::run()
{
    // create CV detector
    CvDetector face_detector = CvDetector(darknet_cfg, darknet_weights);
    // create SORT tracker
    Tracker tracker;

    // 配置GStreamer参数
    std::string pipeline = get_usb_pipeline(capture_width, capture_height);
    qDebug() << "Using pipeline: \t" << QString::fromStdString(pipeline) << "\n";

    //    capture.open(pipeline, cv::CAP_GSTREAMER);
    // capture = cv::VideoCapture(0);
    capture = cv::VideoCapture("/home/user/Videos/city.mp4");
    Size size = Size(int(capture.get(CAP_PROP_FRAME_WIDTH)), int(capture.get(CAP_PROP_FRAME_HEIGHT)));
    int myFourCC = VideoWriter::fourcc('m', 'p', '4', 'v'); // mp4
    double rate = capture.get(CAP_PROP_FPS);
    VideoWriter writer("result.mp4", myFourCC, rate, size, true);

    vector<cv::Scalar> colors;
    // Generate random colors to visualize different bbox
    std::random_device rd;  // Will be used to obtain a seed for the random number engine
    std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
    constexpr int max_random_value = 20;
    std::uniform_int_distribution<> dis(0, max_random_value);
    constexpr int factor = 255 / max_random_value;
    for (int n = 0; n < kNumColors; ++n)
    {
        // Use dis to transform the random unsigned int generated by gen into an int in [0, 7]
        colors.emplace_back(cv::Scalar(dis(gen) * factor, dis(gen) * factor, dis(gen) * factor));
    }

    int frame_index = 0;
    while (capture.isOpened())
    {
        frame_index += 1;
        QElapsedTimer time;
        time.start();
        capture.read(frame);
        qDebug() << " Frame time:  " << time.elapsed() << "ms";

        vector<int> classIds;
        vector<float> confidences;
        vector<cv::Rect> boxes;

        QElapsedTimer time1;
        time1.start();
        face_detector.predict(frame, classIds, confidences, boxes);
        qDebug() << " Detect time:  " << time1.elapsed() << "ms";

        QElapsedTimer time2;
        time2.start();
        /*** Run SORT tracker ***/
        const auto &detections = boxes;
        tracker.Run(detections);
        const auto tracks = tracker.GetTracks();
        /*** Tracker update done ***/
        qDebug() << " SORT time:  " << time2.elapsed() << "ms";

        QElapsedTimer time3;
        time3.start();
        for (int i = 0; i < int(detections.size()); i++)
        {
            vector<Point> temp;
            vecPoint.push_back(temp);
        }

        for (auto &trk : tracks)
        {
            // only draw tracks which meet certain criteria

            if (trk.second.coast_cycles_ < kMaxCoastCycles &&
                (trk.second.hit_streak_ >= kMinHits || frame_index < kMinHits))
            {
                const auto &bbox = trk.second.GetStateAsBbox();

                // Draw lines
                Point center;
                center = Point(bbox.x + bbox.width / 2, bbox.y + bbox.height / 2);
                unsigned long index = unsigned(trk.first);
                vecPoint[index].push_back(center);
                vector<Point> points = vecPoint[index];
                int line_length = 200;
                if (points.size() > 0)
                {
                    for (int j = (points.size() <= unsigned(line_length)) ? 1 : int(points.size()) - line_length; j < int(points.size()); j++)
                    {
                        cv::line(frame, points[unsigned(j)], points[unsigned(j - 1)], colors[index % kNumColors], 2, 8, 0);
                    }
                }

                // Draw boxes and texts
                cv::putText(frame, std::to_string(index + 1), cv::Point(bbox.tl().x, bbox.tl().y - 10),
                            cv::FONT_HERSHEY_DUPLEX, 1, colors[index % kNumColors], 2);
                cv::rectangle(frame, bbox, colors[index % kNumColors], 2);
            }
        }
        qDebug() << " Draw time:  " << time3.elapsed() << "ms";

        writer << frame;
        oriImg = fromBGR2Image(frame);
        if (flag == 0)
        {
            break;
        }
        qDebug() << " Total time  " << time.elapsed() << "ms";
        qDebug() << " FPS:  " << 1000 / time.elapsed();
        emit sendOriImg(oriImg);
    }
    capture.release(); // 释放内存；
}

QImage Cthread_video::fromBGR2Image(cv::Mat oriMat)
{
    QImage src;
    src = QImage(const_cast<unsigned char *>(oriMat.data),
                 oriMat.cols, oriMat.rows,
                 QImage::Format_RGB888)
              .rgbSwapped();
    return src;
}

std::string Cthread_video::get_csi_pipeline(int capture_width, int capture_height, int display_width, int display_height, int framerate)
{
    return "nvarguscamerasrc ! video/x-raw(memory:NVMM), width=(int)" + std::to_string(capture_width) + ", height=(int)" +
           std::to_string(capture_height) + ", format=(string)NV12, framerate=(fraction)" + std::to_string(framerate) +
           "/1 ! nvvidconv flip-method=0 ! video/x-raw, width=(int)" + std::to_string(display_width) + ", height=(int)" + std::to_string(display_height) + ", format=(string)BGRx ! videoconvert ! video/x-raw, format=(string)BGR ! appsink";
}

std::string Cthread_video::get_usb_pipeline(int capture_width, int capture_height)
{
    return " v4l2src device=/dev/video0 ! "
           "video/x-raw, width=(int)" +
           std::to_string(capture_width) + ", height=(int)" + std::to_string(capture_height) + ", "
                                                                                               "format=(string)RGB ! "
                                                                                               "videoconvert ! appsink";
}

void Cthread_video::closeCamera()
{
    flag = 0;
}

// Draw the predicted bounding box
void Cthread_video::drawPred(int classId, double conf, int left, int top, int right, int bottom, Mat &frame)
{
    // Draw a rectangle displaying the bounding box
    rectangle(frame, Point(left, top), Point(right, bottom), Scalar(0, 0, 255));

    // Get the label for the class name and its confidence
    string label = format("%.2f", conf);
    if (!class_labels.empty())
    {
        // assert(classId < (int)classes.size());
        label = class_labels[unsigned(classId)] + ":" + label;
    }

    // Display the label at the top of the bounding box
    int baseLine;
    Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
    top = max(top, labelSize.height);
    putText(frame, label, Point(left, top), FONT_HERSHEY_SIMPLEX, 0.5, Scalar(255, 255, 255));
}
