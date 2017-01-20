#include <iostream>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>
#include <librealsense/rs.hpp>

class RealSenseCamera {
public:
  RealSenseCamera();
  ~RealSenseCamera();
  std::vector<std::string> GetDeviceNames();
  void ActivateDevice(int idx);
  bool GetFrame(cv::Mat& rgb, cv::Mat& depth, double& rgbTs, double& depthTs);
  std::string GetCalibrationString();

private:
  rs::context ctx;
  rs::device* dev;

  int dHeight, dWidth, cHeight, cWidth, dFPS, cFPS;
};

RealSenseCamera::RealSenseCamera() : dev(NULL) {}
RealSenseCamera::~RealSenseCamera() {
  if (!dev) return;
  dev->stop();
}

std::vector<std::string> RealSenseCamera::GetDeviceNames() {
  int nDevices = ctx.get_device_count();

  if (nDevices == 0) return std::vector<std::string>();

  std::vector<std::string> deviceNames;
  for (int i=0; i<nDevices; i++) {
    rs::device* device = ctx.get_device(i);
    std::string name = std::string(device->get_name()) + ": " +
      std::string(device->get_serial());
    deviceNames.push_back(name);
  }
  return deviceNames;
}

void RealSenseCamera::ActivateDevice(int idx) {
  dev = ctx.get_device(idx);
  
  cHeight = 480; // color height, width
  cWidth = 640;
  dHeight = 480; // depth height, width
  dWidth = 640;
  dFPS = 30; // frames per second
  cFPS = 30;

  dev->enable_stream(rs::stream::depth, dWidth, dHeight, rs::format::z16, dFPS);
  dev->enable_stream(rs::stream::color, cWidth, cHeight, rs::format::bgr8, cFPS);
  dev->start();
}

bool RealSenseCamera::GetFrame(cv::Mat& rgb, cv::Mat& depth, double& rgbTs,
                               double& depthTs)
{
  if (!dev) return false;
  dev->wait_for_frames();
  rgbTs = dev->get_frame_timestamp(rs::stream::rectified_color);
  depthTs = dev->get_frame_timestamp(rs::stream::depth_aligned_to_rectified_color);
  
  cv::Mat dep(dHeight, dWidth, CV_16UC1,
    (unsigned char*) dev->get_frame_data(rs::stream::depth_aligned_to_rectified_color));
  cv::Mat color(cHeight, cWidth, CV_8UC3,
    (unsigned char*) dev->get_frame_data(rs::stream::rectified_color));
  depth = dep;
  rgb = color;
  return true;
}

std::string RealSenseCamera::GetCalibrationString() {
  char str[1000];
  std::string fmt = "c_fx=%.4f, c_fy=%.4f, c_px=%.4f, c_py=%.4f";
  rs::intrinsics cI = dev->get_stream_intrinsics(rs::stream::rectified_color);
  sprintf(str, fmt.c_str(), cI.fx, cI.fy, cI.ppx, cI.ppy);
  return std::string(str);
}
