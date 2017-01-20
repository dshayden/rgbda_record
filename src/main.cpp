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

bool isRecording = false;

vector<RealSenseCamera*> cameras;
// vector<openni::VideoStream*> colorStreams;
// vector<openni::VideoStream*> depthStreams;

using std::chrono::time_point;
using std::chrono::system_clock;
using std::chrono::microseconds;

using time_stamp = time_point<system_clock, microseconds>;

std::string windowName = "RGBDA Recorder, Status: Not Recording, Press (R to "
"record, Q to close)";
std::string windowNameRecording = "RGBDA Recorder, Status: Recording, Press (Q "
"to end recording and close program)";

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

int tsSubtract = 0; // time (ms) to subtract from frame timestamps, is the
                    // difference of the time capture and recording began.
void VideoWriterThread() {
  int allW = 1280, allH = 480;
  if (nDevices == 2) allH = 960;

  // cv::Mat dep1(480, 640, CV_8UC1);
  // cv::Mat dep2(480, 640, CV_8UC1);
  // cv::Mat allImg(allH, allW, CV_8UC3);
  // cv::Mat roiC1 = allImg(cv::Rect(0,0,640,480));
  // cv::Mat roiD1 = allImg(cv::Rect(640,0,640,480));
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

    videoWriter->AddFrame(f.colorImgs[0], f.colorTs[0] - tsSubtract);
    depthWriter->AddFrame(f.depthImgs[0], f.depthTs[0] - tsSubtract);
    
    std::string tsStr;
    for (int i=0; i<nDevices-1; i++) {
      tsStr += (std::to_string(f.colorTs[i] - tsSubtract) + ", " +
                std::to_string(f.depthTs[i] - tsSubtract) + ", ");
    }
    tsStr += (std::to_string(f.colorTs[nDevices-1]) + ", " +
              std::to_string(f.depthTs[nDevices-1]) + "\n");

    logFile << tsStr;

    totalFrames++;
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

    // totalFrames++;
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

int main(int argc, const char * argv[]) {
  std::string recordPath = "";
  if (argc == 2) {
    recordPath = argv[1];
#ifdef _WIN32
    recordPath += "\\";
#else
    recordPath += "/";
#endif
  }

  frameCaptureIdx.store(0);
  frameDisplayIdx.store(-1);
  frameFileIdx.store(-1);
  nDevices = 0;

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


  std::thread captureThread(CaptureThread);
  std::thread* fileThread = NULL;
  std::thread* audioThread = NULL;

  AudioRecorder ar;
  Mat depMat;
  int allH = 480, allW = 640*2;
  Mat allImg(allH, allW, CV_8UC3);
  Mat roiC1 = allImg(cv::Rect(0,0,640,480));
  Mat roiD1 = allImg(cv::Rect(640,0,640,480));

  cv::namedWindow(windowName, CV_GUI_NORMAL | CV_WINDOW_AUTOSIZE |
    CV_WINDOW_KEEPRATIO);

  while (endProgram.load() == 0) {
    int capIdx = frameCaptureIdx.load();

    if (endProgram.load()) break;
    if (capIdx <= 0 || frameDisplayIdx.load() == capIdx-1) continue;
    else frameDisplayIdx.store(capIdx-1);

    RgbdFrame& f = frames[frameDisplayIdx.load()];
    f.colorImgs[0].copyTo(roiC1);
    f.depthImgs[0].convertTo(depMat, CV_8U, 255.0 / 10000);
    applyColorMap(depMat, roiD1, cv::COLORMAP_JET);
    cv::imshow(windowName, allImg);

    int key = cv::waitKey(30);
    if ( key == 'q'  || key == 'Q' ) break;
    if ((key == 'r'  || key == 'R') && !isRecording) {

      int capIdx = frameCaptureIdx.load();
      frameFileIdx.store(min(0, capIdx-2));

      RgbdFrame& f = frames[(capIdx-1) % nFrames];
      tsSubtract = f.colorTs[0]; // So timestamps start from 0 at recording.

      std::string curTime = GetCurrentTimeAsString();
      std::string fname = recordPath + "recording-" + curTime;

      // Record camera intrinsics
      try {
        // Open video file in main thread (ffmpeg won't write to it otherwise)
        videoWriter = new BgrVideoWriter(fname + ".mp4", 30, 640, 480);
        depthWriter = new DepthVideoWriter(fname + ".avi", 30, 640, 480);

        logFile.open(fname + ".log", std::ofstream::out);
        logFile << cameras[0]->GetCalibrationString() << std::endl;

      } catch (...) {
        cout << "Could not write to directory: " << recordPath << endl;
        DeInitCamera();
        exit(1);
      }
      
      // Begin video and audio encoding threads.
      fileThread = new std::thread(VideoWriterThread);
      audioThread = ar.RecordInAnotherThread(fname + ".ogg");

      cv::setWindowTitle(windowName, windowNameRecording);

      isRecording = true;
    }
  }
  endProgram.store(1);

  if (isRecording) {
    ar.EndRecord();
    if (audioThread) audioThread->join();
    if (fileThread) fileThread->join();
    logFile.close();
  }
  captureThread.join();

  try {
    DeInitCamera();
  } catch (const std::exception& e) {
    std::cout << "Caught exception \"" << e.what() << "\"\n";
    cout << "Warning: It may be necessary to unplug and replug cameras before "
      "they can be used again.\n";
  }

  cout << "Total Frames Recorded: " << totalFrames << endl;
  return 0;
}
