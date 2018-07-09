/*
 * Copyright (C) 2017- David S. Hayden - All Rights Reserved.
*/


#include <iostream>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/registration.h>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/logger.h>

std::string WriteParamsToString(std::string serial, std::string firmware,
                            libfreenect2::Freenect2Device::Config cfg,
                            libfreenect2::Freenect2Device::IrCameraParams ip,
                            libfreenect2::Freenect2Device::ColorCameraParams cp);

class Kinect2Camera {
public:
  Kinect2Camera(std::string whichPipeline);
  ~Kinect2Camera();
  std::vector<std::string> GetDeviceNames();
  void ActivateDevice(int idx);
  bool GetFrame(cv::Mat& rgb, cv::Mat& depth, double& rgbTs, double& depthTs);
  std::string GetCalibrationString();

private:
  libfreenect2::Freenect2 ctx;
  libfreenect2::Freenect2Device* dev;
  libfreenect2::PacketPipeline* pipeline;
  libfreenect2::SyncMultiFrameListener* listener;
  libfreenect2::FrameMap* frames;
  libfreenect2::Freenect2Device::Config cfg;

  std::string serial = "";

  int dHeight, dWidth, cHeight, cWidth, dFPS, cFPS;
};

Kinect2Camera::Kinect2Camera(std::string whichPipeline)
  : dev(NULL), pipeline(NULL) {
  // pipeline = new libfreenect2::OpenGLPacketPipeline();
  pipeline = new libfreenect2::OpenCLPacketPipeline(2);
  // pipeline = new libfreenect2::CpuPacketPipeline();

  frames = new libfreenect2::FrameMap;

  cfg.MinDepth = 1;
  cfg.MaxDepth = 6;
  cfg.EnableBilateralFilter = true;
  cfg.EnableEdgeAwareFilter = true;
}

Kinect2Camera::~Kinect2Camera() {
  if (!dev) return;
  dev->stop();
  dev->close();
}

std::vector<std::string> Kinect2Camera::GetDeviceNames() {
  int nDevices = ctx.enumerateDevices();

  if (nDevices == 0) return std::vector<std::string>();

  std::vector<std::string> deviceNames;
  for (int i=0; i<nDevices; i++) {
    deviceNames.push_back("Kinect2: " + ctx.getDeviceSerialNumber(i));
  }
  return deviceNames;
}

void Kinect2Camera::ActivateDevice(int idx) {
  dev = ctx.openDevice(ctx.getDeviceSerialNumber(idx), pipeline);
  dev->setConfiguration(cfg);

  int streamTypes = libfreenect2::Frame::Color | libfreenect2::Frame::Depth |
    libfreenect2::Frame::Ir;
  listener = new libfreenect2::SyncMultiFrameListener(streamTypes);

  dev->setColorFrameListener(listener);
  dev->setIrAndDepthFrameListener(listener);
  dev->start();
}

bool Kinect2Camera::GetFrame(cv::Mat& rgb, cv::Mat& depth, double& rgbTs,
                               double& depthTs)
{
  if (!dev) {
    std::cout << "device null, bad\n";
    return false;
  }
  listener->waitForNewFrame(*frames);

  libfreenect2::Frame *rgbK = (*frames)[libfreenect2::Frame::Color];
  libfreenect2::Frame *irK = (*frames)[libfreenect2::Frame::Ir];
  libfreenect2::Frame *depthK = (*frames)[libfreenect2::Frame::Depth];

  cv::Mat colorMat(rgbK->height, rgbK->width, CV_8UC4, rgbK->data);
  cv::Mat depMat(depthK->height, depthK->width, CV_32FC1, depthK->data);

  // todo: add method to call listener->release so no extra copy here
  // colorMat.copyTo(rgb);
  cv::resize(colorMat, rgb, rgb.size(), 0, 0);

  depMat.convertTo(depth, CV_16UC1);

  listener->release(*frames);

  rgbTs = rgbK->timestamp * 0.125;
  depthTs = depthK->timestamp * 0.125;
  return true;
}

std::string Kinect2Camera::GetCalibrationString() {
  if (!dev) return "";
  return WriteParamsToString(dev->getSerialNumber(), dev->getFirmwareVersion(),
      cfg, dev->getIrCameraParams(),
      dev->getColorCameraParams());
  // return "";
}

std::string WriteParamsToString(std::string serial, std::string firmware,
                            libfreenect2::Freenect2Device::Config cfg,
                            libfreenect2::Freenect2Device::IrCameraParams ip,
                            libfreenect2::Freenect2Device::ColorCameraParams cp)
{
  std::stringstream str;
  str << "1.0" << std::endl;
  str << serial << std::endl << firmware << std::endl;
  str << cfg.MinDepth << " " << cfg.MaxDepth << " " <<
    cfg.EnableBilateralFilter << " " << cfg.EnableEdgeAwareFilter << std::endl;

  str << ip.fx << " "
      << ip.fy << " "
      << ip.cx << " "
      << ip.cy << " "
      << ip.k1 << " "
      << ip.k2 << " "
      << ip.k3 << " "
      << ip.p1 << " "
      << ip.p2 << "\n";

  str << cp.fx << " "
      << cp.fy << " "
      << cp.cx << " "
      << cp.cy << " "
      << cp.shift_d << " "
      << cp.shift_m << " "
      << cp.mx_x3y0 << " "
      << cp.mx_x0y3 << " "
      << cp.mx_x2y1 << " "
      << cp.mx_x1y2 << " "
      << cp.mx_x2y0 << " "
      << cp.mx_x0y2 << " "
      << cp.mx_x1y1 << " "
      << cp.mx_x1y0 << " "
      << cp.mx_x0y1 << " "
      << cp.mx_x0y0 << " "
      << cp.my_x3y0 << " "
      << cp.my_x0y3 << " "
      << cp.my_x2y1 << " "
      << cp.my_x1y2 << " "
      << cp.my_x2y0 << " "
      << cp.my_x0y2 << " "
      << cp.my_x1y1 << " "
      << cp.my_x1y0 << " "
      << cp.my_x0y1 << " "
      << cp.my_x0y0;

  return str.str();
}
