#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <time.h>
#include <inttypes.h>
#include <thread>
#include <atomic>
#include <stdio.h>
#include <fstream>
#include <chrono>

#ifdef _WIN32
#include <windows.h>
#endif


#include "RealSenseCamera.hpp"

#include <opencv2/opencv.hpp>
#include "opencv2/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/videoio.hpp"

#include "VideoWriter.hpp"
#include "audiorecorder.hpp"

using cv::Mat;
using std::vector;
using std::cout;
using std::cerr;
using std::endl;

struct RgbdFrame {
  vector<cv::Mat> colorImgs;
  vector<cv::Mat> depthImgs;

  vector<uint64_t> colorTs;
  vector<uint64_t> depthTs;
};

const int nFrames = 300;
vector<RgbdFrame> frames(nFrames);

int64_t totalFrames = 0;
int64_t frameDrops = 0;

// atomic operators
std::atomic_int frameCaptureIdx;
std::atomic_int frameDisplayIdx;
std::atomic_int frameFileIdx;
std::atomic_int endProgram;

// RgbVideoWriter* videoWriter = NULL;
BgrVideoWriter* videoWriter = NULL;
DepthVideoWriter* depthWriter = NULL;
std::ofstream logFile;

int nDevices;

vector<RealSenseCamera*> cameras;
// vector<openni::VideoStream*> colorStreams;
// vector<openni::VideoStream*> depthStreams;

using std::chrono::time_point;
using std::chrono::system_clock;
using std::chrono::microseconds;

using time_stamp = time_point<system_clock, microseconds>;

std::string windowName = "RGBDA Recorder (q to close)";

#ifdef _WIN32
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam){
  switch (uMsg) {
    case WM_CLOSE: // user clicks the 'x'
      endProgram.store(1);
      DestroyWindow(hwnd);
      break;
    case WM_DESTROY:
      endProgram.store(1);
      PostQuitMessage(0);
      break;
    default:
      return DefWindowProc(hwnd, uMsg, wParam, lParam);
  }
}
#endif


void VideoWriterThread() {
  int allW = 1280, allH = 480;
  if (nDevices == 2) allH = 960;

  cv::Mat dep1(480, 640, CV_8UC1);
  cv::Mat dep2(480, 640, CV_8UC1);
  cv::Mat allImg(allH, allW, CV_8UC3);
  cv::Mat roiC1 = allImg(cv::Rect(0,0,640,480));
  cv::Mat roiD1 = allImg(cv::Rect(640,0,640,480));

  // cv::Mat roiC2 = roiC1;
  // cv::Mat roiD2 = roiD1;

  while (true) {
    int capIdx = frameCaptureIdx.load();
    int fileIdx = frameFileIdx.load(); 

    if (endProgram.load()) break;
    if (capIdx <= 0 || fileIdx == capIdx-1) {
      continue;
    }

    frameFileIdx.store((fileIdx+1) % nFrames);
    fileIdx = frameFileIdx.load();

    RgbdFrame& f = frames[fileIdx];

    videoWriter->AddFrame(f.colorImgs[0], f.colorTs[0]);
    depthWriter->AddFrame(f.depthImgs[0], f.depthTs[0]);
    
    std::string tsStr;
    for (int i=0; i<nDevices-1; i++) {
      tsStr += (std::to_string(f.colorTs[i]) + ", " +
                std::to_string(f.depthTs[i]) + ", ");
    }
    tsStr += (std::to_string(f.colorTs[nDevices-1]) + ", " +
              std::to_string(f.depthTs[nDevices-1]) + "\n");

    logFile << tsStr;
  }

  delete videoWriter;
  videoWriter = NULL;

  delete depthWriter;
  depthWriter = NULL;
}

void CaptureThread() {
  // Work
  cv::Mat rgb, depth;
  double rgbTs, depthTs;
  while (true) {
    int capIdx = frameCaptureIdx.load();
    bool isValid = true;
    for (int i=0; i<nDevices; i++) {
      bool status = cameras[i]->GetFrame(rgb, depth, rgbTs, depthTs);
      if (!status) {
        cout << "Error in camera capture." << endl;
        break;
      }
      rgb.copyTo(frames[capIdx].colorImgs[i]);
      depth.copyTo(frames[capIdx].depthImgs[i]);

      frames[capIdx].colorTs[i] = rgbTs;
      frames[capIdx].depthTs[i] = depthTs;
    }

    totalFrames++;
    frameCaptureIdx.store((capIdx+1) % nFrames);
    if (endProgram.load()) break;
  }
}

std::string GetCurrentTimeAsString() {
  time_t     now = time(0);
  struct tm  tstruct;
  char       buf[80];
  tstruct = *localtime(&now);

  // See http://en.cppreference.com/w/cpp/chrono/c/strftime
  strftime(buf, sizeof(buf), "%Y-%m-%d_%H_%M_%S", &tstruct);

  std::string str(buf);
  return str;
}

bool InitCamera() {
  cout << cameras.size() << endl;
  cameras.push_back(new RealSenseCamera());
  std::vector<std::string> devices = cameras[0]->GetDeviceNames();
  if (devices.size()==0) {
    cout << "No supported cameras are connected. Exiting\n";
    exit(1);
  }
  cameras[0]->ActivateDevice(0);
  cout << "Using " << devices[0] << endl;
  nDevices = 1;
  return true;
}

void DeInitCamera() {
  for (int i=0; i<cameras.size(); i++) {
    if (cameras[i]) delete cameras[i];
  }
}

void onMouse(int event, int x, int y, int, void* userdata) {
  if (event != cv::EVENT_LBUTTONUP || y > 30) return; 
  cv::displayOverlay(windowName, "(Recording): Click Here to End Recording", 0);
}

int main(int argc, const char * argv[]) {
  frameCaptureIdx.store(0);
  frameDisplayIdx.store(-1);
  frameFileIdx.store(-1);
  nDevices = 0;

  std::string curTime = GetCurrentTimeAsString();
  cout << "Current Time: " << curTime << endl;

  // Set camera up in separate thread so we can later use it in separate thread
  std::thread initThread(InitCamera);
  initThread.join();

  // Pre-allocate all RgbdFrames, using hard-coded width/height for now.
  for (int i=0; i<nFrames; i++) {
    frames[i].colorImgs = vector<cv::Mat>(nDevices);
    frames[i].depthImgs = vector<cv::Mat>(nDevices);

    frames[i].colorTs = vector<uint64_t>(nDevices);
    frames[i].depthTs = vector<uint64_t>(nDevices);

    for (int j=0;j<nDevices;j++) {
      frames[i].colorImgs[j] = cv::Mat(480, 640, CV_8UC3);
      frames[i].depthImgs[j] = cv::Mat(480, 640, CV_16UC1);

      frames[i].colorTs[j] = 0;
      frames[i].depthTs[j] = 0;
    }
  }

  std::string fname = "recording-" + curTime;

  // Open video file; note: ffmpeg won't write the file if not initialized in
  // the main thread.
  videoWriter = new BgrVideoWriter(fname + ".mp4", 30, 640, 480);
  depthWriter = new DepthVideoWriter(fname + ".avi", 30, 640, 480);
  logFile.open(fname + ".ts", std::ofstream::out);
  logFile << cameras[0]->GetCalibrationString() << std::endl;

  std::thread captureThread(CaptureThread);
  std::thread fileThread(VideoWriterThread);

  AudioRecorder ar;
  std::thread* audioThread = ar.RecordInAnotherThread(fname + ".ogg");

  Mat depMat;

  int allH = 480, allW = 640*2;
  Mat allImg(allH, allW, CV_8UC3);
  Mat roiC1 = allImg(cv::Rect(0,0,640,480));
  Mat roiD1 = allImg(cv::Rect(640,0,640,480));

  cv::namedWindow(windowName, CV_GUI_NORMAL | CV_WINDOW_AUTOSIZE |
    CV_WINDOW_KEEPRATIO);
  cv::displayOverlay(windowName, "Recording, Press q to end recording and close "
      "program", 0);
  // cv::displayOverlay(windowName, "(Not Recording): Click Here to Record", 0);
  // cv::setMouseCallback(windowName, onMouse, 0);

  bool isRecording = false;
  while (true) {
    int capIdx = frameCaptureIdx.load();

    if (endProgram.load()) break;
    if (capIdx <= 0 || frameDisplayIdx.load() == capIdx-1) continue;
    else frameDisplayIdx.store(capIdx-1);

    RgbdFrame& f = frames[frameDisplayIdx.load()];
    f.colorImgs[0].copyTo(roiC1);
    f.depthImgs[0].convertTo(depMat, CV_8U, 255.0 / 10000);
    applyColorMap(depMat, roiD1, cv::COLORMAP_JET);
    cv::imshow("RGBDA Recorder (q to close)", allImg);

    int key = cv::waitKey(30);
    if ( key == 'q'  || key == 'Q' ) break;
    // if ((key == 'r'  || key == 'R') && !isRecording) {
    //   cout << "Begin recording..." << endl;
    //   isRecording = true;
    // }
  }
  endProgram.store(1);
  
  ar.EndRecord();
  if (audioThread)
    audioThread->join();

  captureThread.join();
  fileThread.join();
  logFile.close();

  DeInitCamera();
  cout << "Total Frames: " << totalFrames << endl;
  return 0;
}
