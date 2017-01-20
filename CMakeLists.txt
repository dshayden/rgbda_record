cmake_minimum_required (VERSION 2.8.11)

set(PROG_NAME "rgbda_record")
project (${PROG_NAME})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
set(CMAKE_CPP_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

IF(APPLE)
  link_directories("thirdparty/realsense/lib")
  include_directories("thirdparty/realsense/include")

  add_executable(${PROG_NAME} MACOSX_BUNDLE src/main.cpp src/audiorecorder.cpp
    src/VideoWriter.cpp)
  target_link_libraries(${PROG_NAME} stdc++ opencv_highgui opencv_imgproc
    opencv_core opencv_imgcodecs opencv_videoio avformat avutil avcodec
    swscale portaudio sndfile realsense)

  set_target_properties(${PROG_NAME} PROPERTIES
    MACOSX_BUNDLE_NAME ${PROG_NAME}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROG_NAME}
    MACOSX_BUNDLE_LONG_VERSION_STRING ${PROG_NAME})
  # target_compile_definitions(${PROG_NAME} PRIVATE MAC_OS=1)

  file(COPY "thirdparty/realsense/lib/" DESTINATION "${PROG_NAME}.app/Contents/Resources")
ELSEIF(WIN32)
  link_directories("C:\\dev\\opencv\\build\\x64\\vc14\\lib")
  include_directories("C:\\dev\\opencv\\build\\include")

  include_directories("C:\\dev\\ffmpeg-3.2.2-win64-dev\\Include")
  link_directories("C:\\dev\\ffmpeg-3.2.2-win64-dev\\lib")

  include_directories("C:\\dev\\libsndfile\\include")
  link_directories("C:\\dev\\libsndfile\\lib")

  include_directories("C:\\dev\\portaudio-r1891-build\\include")
  link_directories("C:\\dev\\portaudio-r1891-build\\lib\\x64\\Release")

  include_directories("C:\\dev\\intel_realsense\\include")
  link_directories("C:\\dev\\intel_realsense\\lib")

  add_executable(${PROG_NAME} src/main.cpp src/audiorecorder.cpp
    src/VideoWriter.cpp)
  target_link_libraries(${PROG_NAME} opencv_world310 avformat avutil
    avcodec swscale portaudio_x64 libsndfile-1 realsense)
ENDIF()
