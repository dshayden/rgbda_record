Description
This folder contains the rgbda_record program, which handles simultaneously
capturing and compressing an RGB-D stream from an Intel RealSense camera. The
program is built to run on Windows 10 64-bit, and will only start if an Intel
RealSense camera is plugged in. When this is the case, the program will show
the RGB and Depth streams live and, when the user presses 'r' will begin
recording to several files, named as:
  recording-<timestamp>.mp4 : H264-compressed RGB stream
  recording-<timestamp>.avi : (Lossless) FFV1-compressed depth stream
  recording-<timestamp>.ogg : Ogg Vorbis-compressed audio stream
  recording-<timestamp>.log : File with camera intrinsics and frame timestamps
                              (in milliseconds, relative to recording start)

Installation and Use
Before running the program or plugging any cameras in, do the following:
1) Install the Intel RealSense R200 Camera Driver from Intel:
   https://software.intel.com/en-us/intel-realsense-sdk/download
2) Install the Visual C++ Redistributable for Visual Studio 2015 from Microsft:
   https://www.microsoft.com/en-us/download/details.aspx?id=48145
3) Run the rgbda_record app. Windows may warn you that the application is from
   an unknown publisher; you will need to say it's safe to run.
4) When the program is running, you will get a streaming view of RGB and depth
   streams. To start recording, press R. Files will be recorded to the directory
   that the rgba_record app is ran from.
     NOTE: You can change the location that files are recorded to by passing the
           directory you wish to record in as the first program argument.
