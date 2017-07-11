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
#include "Kinect2Camera.hpp"

#include <opencv2/opencv.hpp>
#include "opencv2/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/videoio.hpp"

#include "VideoWriter.hpp"
#include "audiorecorder.hpp"

#include "ezOptionParser.hpp"

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

  long collectionTs;
};

std::chrono::time_point<std::chrono::high_resolution_clock> progStartTime;

const int nFrames = 300;

vector<RgbdFrame> frames(nFrames);

int64_t totalFrames = 0;
int64_t frameDrops = 0;

// atomic operators
std::atomic_int frameCaptureIdx; // most recently captured frame
std::atomic_int frameDisplayIdx;
std::atomic_int frameFileIdx; // last written frame
std::atomic_int endProgram;

RgbVideoWriter* videoWriter = NULL;
DepthVideoWriter* depthWriter = NULL;

// cv::VideoWriter* videoWriter = NULL;
// cv::VideoWriter* depthWriter = NULL;

std::ofstream logFile;

int nDevices;

bool isRecording = false;

vector<Kinect2Camera*> cameras;
// vector<RealSenseCamera*> cameras;
// vector<openni::VideoStream*> colorStreams;
// vector<openni::VideoStream*> depthStreams;

int rgbH = 1080, rgbW = 1920;
// int rgbH = 720, rgbW = 1280;
// int rgbH = 540, rgbW = 960;
int depH = 424, depW = 512;

using std::chrono::time_point;
using std::chrono::system_clock;
using std::chrono::microseconds;

using time_stamp = time_point<system_clock, microseconds>;

std::string windowName = "RGBDA Recorder, Status: Not Recording, Press (R to "
"record, Q to close)";
std::string windowNameRecording = "RGBDA Recorder, Status: Recording, Press (Q "
"to end recording and close program)";
std::string windowNameClosing =
  "RGBDA Recorder, Status: Closing (please be patient, may take ~10 seconds)";

std::string contolWindowName = "Camera Controls";

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

#define modulo_positive(x,n) (n + (x % n)) % n

static unsigned long getTimeSinceEpochMillis(std::time_t* t = nullptr)
{
  return static_cast<unsigned long>
    (static_cast<std::chrono::milliseconds>(std::time(t)).count());
}

uint64_t tsSubtract = 0; // time (ms) to subtract from frame timestamps, is the
                         // difference of the time capture and recording began.
void VideoWriterThread() {

  while (true) {
    int capIdx = frameCaptureIdx.load();
    int fileIdx = frameFileIdx.load(); 

    if (endProgram.load() && fileIdx == capIdx) break;
    if (fileIdx == capIdx) continue;

    // if (endProgram.load() && fileIdx == modulo_positive(capIdx-1,nFrames) ) {
    //   break;
    // }
    // if (capIdx <= 0 || fileIdx == capIdx-1) {
    //   continue;
    // }

    fileIdx = (fileIdx+1) % nFrames;
    RgbdFrame& f = frames[fileIdx];

    // auto start=std::chrono::high_resolution_clock::now();
    // // videoWriter->write(f.colorImgs[0]);
    // // cout << "about to call depthWriter->write" << endl;
    // // depthWriter->write(f.depthImgs[0]);
    // auto diff = std::chrono::duration_cast<std::chrono::milliseconds>
    //   (std::chrono::high_resolution_clock::now()-start);
    // auto cnt = diff.count();
    // if (cnt!=0) {
    //   std::cout << "Write FPS: " << 1000/float(cnt) << std::endl;
    // }

    videoWriter->AddFrame(f.colorImgs[0], f.colorTs[0] - tsSubtract);
    depthWriter->AddFrame(f.depthImgs[0], f.depthTs[0] - tsSubtract);

    // if (totalFrames > 0 && totalFrames % 100 == 0) {
    //   videoWriter->WriteBuffer();
    //   depthWriter->WriteBuffer();
    // }

    // std::string fNameDep = "imgs/" + std::to_string(f.depthTs[0] - tsSubtract) + ".png";
    // std::string fNameRgb = "imgs/" + std::to_string(f.colorTs[0] - tsSubtract) + ".jpg";
    // auto start=std::chrono::high_resolution_clock::now();
    // // cv::imwrite(fNameDep, f.depthImgs[0], depthParams);
    // // cv::imwrite(fNameRgb, f.colorImgs[0], rgbParams);
    // cv::imwrite(fNameDep, f.depthImgs[0]);
    // cv::imwrite(fNameRgb, f.colorImgs[0]);
    // auto diff = std::chrono::duration_cast<std::chrono::milliseconds>
    //   (std::chrono::high_resolution_clock::now()-start);
    // auto cnt = diff.count();
    // if (cnt!=0) {
    //   std::cout << "Write FPS: " << 1000/float(cnt) << std::endl;
    // }
    
    std::string tsStr;
    for (int i=0; i<nDevices-1; i++) {
      tsStr += (std::to_string(f.colorTs[i] - tsSubtract) + ", " +
                std::to_string(f.depthTs[i] - tsSubtract) + ", ");
    }
    tsStr += (std::to_string(f.colorTs[nDevices-1]) + ", " +
              std::to_string(f.depthTs[nDevices-1]) + ", " +
              std::to_string(f.collectionTs) + "\n");
    logFile << tsStr;
    totalFrames++;

    frameFileIdx.store(fileIdx);
  }

  delete videoWriter;
  videoWriter = NULL;

  delete depthWriter;
  depthWriter = NULL;
}

void CaptureThread() {
  // Work
  cv::Mat rgb = cv::Mat(rgbH, rgbW, CV_8UC4);
  cv::Mat depth = cv::Mat(depH, depW, CV_16UC1);
  double rgbTs, depthTs;

  while (true) {
    int capIdx = frameCaptureIdx.load();
    int fileIdx = frameFileIdx.load();

    if (endProgram.load()) break;
    if (((capIdx+1) % nFrames) == fileIdx) continue;

    // if (modulo_positive(fileIdx-1, nFrames) == capIdx) continue;
    capIdx = (capIdx+1) % nFrames;

    bool isValid = true;
    for (int i=0; i<nDevices; i++) {
      bool status = cameras[i]->GetFrame(rgb, depth, rgbTs, depthTs);
      if (!status) {
        cout << "Error in camera capture." << endl;
        break;
      }
      // frames[capIdx].colorImgs[i] = rgb.clone();
      
      // frames[capIdx].collectionTs = getTimeSinceEpochMillis(NULL);
      auto capTime = std::chrono::high_resolution_clock::now();
      frames[capIdx].collectionTs =
        std::chrono::duration_cast<std::chrono::milliseconds>
        (capTime - progStartTime).count();

      rgb.copyTo(frames[capIdx].colorImgs[i]);
      depth.convertTo(frames[capIdx].depthImgs[i], CV_16UC1);

      frames[capIdx].colorTs[i] = rgbTs;
      frames[capIdx].depthTs[i] = depthTs;
    }
    frameCaptureIdx.store(capIdx);

    // totalFrames++;
    // frameCaptureIdx.store((capIdx+1) % nFrames);
    // if (endProgram.load()) break;
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
  // cameras.push_back(new RealSenseCamera());
  cameras.push_back(new Kinect2Camera("cl"));
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
  // Start time of program
  progStartTime = std::chrono::high_resolution_clock::now();

  // Parse Options
  ez::ezOptionParser opt;
  opt.overview = "RGB and Depth Recording App (H.264 Color and Lossless FFV1 Depth)";
  opt.syntax = "rgba_record [-h --help] [--manual] [--crf v] [--preset s] [--output_dir s]";
  opt.example = "rgba_record --crf 23 --preset medium --output imgs\n";
  opt.footer = "Copyright (C) 2016 David S. Hayden\n";

  opt.add(
    "", // Default.
    0, // Required?
    0, // Number of args expected.
    0, // Delimiter if expecting multiple args.
    "Display help", // Help description.
    "-h",     // Flag token. 
    "--help" // Flag token.
  );

  opt.add(
    "options.ini", // Default.
    0, // Required?
    1, // Number of args expected.
    0, // Delimiter if expecting multiple args.
    "File to import arguments.", // Help description.
    "--options_file" // Flag token.
  );

  // opt.add(
  //   "", // Default.
  //   0, // Required?
  //   0, // Number of args expected.
  //   0, // Delimiter if expecting multiple args.
  //   "Enable Manual Camera Control", // Help description.
  //   "--manual" // Flag token.
  // );

  ez::ezOptionValidator* crfValidator = new ez::ezOptionValidator("s4", "gele", "0,51");
  opt.add(
    "18", // Default.
    0, // Required?
    1, // Number of args expected.
    0, // Delimiter if expecting multiple args.
    "Color recording quality. Must be integer between 0..51. Scale is:\n0 (Lossless), 18 (Very Good), 23 (Normal) 51 (Terrible)\nDefault: 20", // Help description.
    "--crf", // Flag token.
    crfValidator
  );

  opt.add(
    "medium", // Default.
    0, // Required?
    1, // Number of args expected.
    0, // Delimiter if expecting multiple args.
    "Color recording quality preset, one of:\n{veryslow, slower, slow, medium, fast, faster, veryfast, superfast, ultrafast}\nDefault: medium", // Help description.
    "--preset" // Flag token.
  );

  string defaultRecordPath = "";
  opt.add(
    "", // Default.
    0, // Required?
    1, // Number of args expected.
    0, // Delimiter if expecting multiple args.
    "Directory to save recordings", // Help description.
    "--output_dir" // Flag token.
  );

  opt.parse(argc, argv);

  if (opt.isSet("-h")) {
    std::string usage;
    opt.getUsage(usage);
    std::cout << usage;
    return 1;
  }

  string options_file;
  opt.get("--options_file")->getString(options_file);
  if (!opt.importFile(options_file.c_str(), '#')) {
    cout << "WARNING: Failed to open options file: " << options_file << endl;
  }
  
  // string prettyprint;
  // opt.prettyPrint(prettyprint);
  // cout << prettyprint;
  // opt.exportFile("options.ini", true);

  std::string recordPath = "";
  if (opt.isSet("--output_dir")) {
    opt.get("--output_dir")->getString(recordPath);
#ifdef _WIN32
    recordPath += "\\";
#else
    recordPath += "/";
#endif
  }

  std::vector<std::string> badOptions, badArgs;
  if (!opt.gotValid(badOptions, badArgs)) {
    for(int i=0; i < badOptions.size(); ++i)
      std::cerr << "ERROR: Got invalid argument \"" << badArgs[i] << "\" for option " << badOptions[i] << ".\n\n";
    exit(1);
  }

  string crfValue;
  opt.get("--crf")->getString(crfValue);

  string presetValue;
  opt.get("--preset")->getString(presetValue);

  cout << "Color Recording (crf: " << crfValue << ", preset: " << presetValue << ")\n";
  vector<string> bgrOpts = {"preset", presetValue, "tune", "film", "crf",
    crfValue, "pixel_format", "yuv420p"};

  // vector<string> bgrOpts = {"preset", presetValue, "tune", "zerolatency", "crf",
  //   crfValue, "pixel_format", "yuv420p", "profile", "baseline"};
  // vector<string> bgrOpts = {"preset", "ultrafast", "tune", "zerolatency", "crf",
  //   crfValue, "pixel_format", "yuv420p", "profile", "baseline"};

  // Finished processing command line options

  // frameCaptureIdx.store(0);
  frameCaptureIdx.store(-1);
  frameDisplayIdx.store(-1);
  frameFileIdx.store(-1);
  nDevices = 0;

  // Set camera up in separate thread so we can later use it in separate thread
  // InitCamera();
  std::thread initThread(InitCamera);
  initThread.join();

  // Pre-allocate all RgbdFrames, using hard-coded width/height for now.
  for (int i=0; i<nFrames; i++) {
    frames[i].colorImgs = vector<cv::Mat>(nDevices);
    frames[i].depthImgs = vector<cv::Mat>(nDevices);

    frames[i].colorTs = vector<uint64_t>(nDevices);
    frames[i].depthTs = vector<uint64_t>(nDevices);

    for (int j=0;j<nDevices;j++) {
      // frames[i].colorImgs[j] = cv::Mat(480, 640, CV_8UC3);
      // frames[i].depthImgs[j] = cv::Mat(480, 640, CV_16UC1);
      frames[i].colorImgs[j] = cv::Mat(1080, 1920, CV_8UC4);
      frames[i].depthImgs[j] = cv::Mat(424, 512, CV_32FC1);

      frames[i].colorTs[j] = 0;
      frames[i].depthTs[j] = 0;
    }
  }


  std::thread captureThread(CaptureThread);
  std::thread* fileThread = NULL;
  std::thread* audioThread = NULL;

  AudioRecorder ar;
  Mat depMat;
  int allH = 424, allW = 1152;
  Mat allImg(allH, allW, CV_8UC3);
  Mat roiC1 = allImg(cv::Rect(0,32,640,360));
  Mat roiD1 = allImg(cv::Rect(640,0,512,424));
  Mat colorResize = cv::Mat(360, 640, CV_8UC4);

  cv::namedWindow(windowName, CV_GUI_NORMAL | CV_WINDOW_OPENGL);

  // if (opt.isSet("--manual")) {
  //   cv::namedWindow(contolWindowName, CV_GUI_NORMAL | CV_WINDOW_AUTOSIZE |
  //     CV_WINDOW_KEEPRATIO);
  //   int initBrightness = 0, brightnessMax = 255;
  //   cv::createTrackbar("Brightness (RGB)", contolWindowName, &initBrightness, brightnessMax, NULL );
  //   cv::createTrackbar("Exposure (RGB)", contolWindowName, &initBrightness, brightnessMax, NULL );
  // }

  int framesDisp = 0;
  while (endProgram.load() == 0) {
    int capIdx = frameCaptureIdx.load();

    if (endProgram.load()) break;
    if (capIdx <= 0 || frameDisplayIdx.load() == capIdx-1) continue;
    else frameDisplayIdx.store(capIdx-1);

    RgbdFrame& f = frames[frameDisplayIdx.load()];
    f.depthImgs[0].convertTo(depMat, CV_8U, 255.0 / 6000);
    
    cv::resize(f.colorImgs[0], colorResize, colorResize.size(), 0, 0, cv::INTER_NEAREST);
    cv::cvtColor(colorResize, roiC1, cv::COLOR_BGRA2BGR);

    applyColorMap(depMat, roiD1, cv::COLORMAP_BONE);
    cv::imshow(windowName, allImg);

    int key = cv::waitKey(30);
    if ( key == 'q'  || key == 'Q' ) {
      cv::setWindowTitle(windowName, windowNameClosing);
      cv::waitKey(1);
      break;
    }
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
        int fps = 25;

        // videoWriter = new cv::VideoWriter(fname + ".mp4", cv::VideoWriter::fourcc('H', '2', '6', '4'), 25, cv::Size(rgbW, rgbH), true);
        // depthWriter = new cv::VideoWriter(fname + ".avi", cv::VideoWriter::fourcc('F', 'F', 'V', '1'), 25, cv::Size(depW, depH), false);

        videoWriter = new RgbVideoWriter(fname + ".mp4", fps, rgbW, rgbH, bgrOpts);
        depthWriter = new DepthVideoWriter(fname + ".avi", fps, depW, depH);

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
