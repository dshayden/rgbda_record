#include <iostream>
#include <vector>

#include <opencv2/opencv.hpp>

using namespace std;

// #define PRId64  "%llu" 

extern "C" {
  #include "libavcodec/avcodec.h"
  #include "libavformat/avformat.h"
  #include "libavformat/avio.h"
  #include "libavutil/mathematics.h"
  #include "libswscale/swscale.h"
  #include "libavutil/opt.h"
  
  AVCodec *avcodec_find_encoder(enum AVCodecID id);
  int avcodec_open2(AVCodecContext *avctx, const AVCodec *codec, AVDictionary **options);
  int avcodec_encode_video2(AVCodecContext *avctx, AVPacket *avpkt,
                                const AVFrame *frame, int *got_packet_ptr);
  AVFormatContext *avformat_alloc_context(void);
  AVStream *avformat_new_stream(AVFormatContext *s, const AVCodec *c);
  int avcodec_get_context_defaults3(AVCodecContext *s, const AVCodec *codec);
	struct SwsContext *sws_getCachedContext(struct SwsContext *context,
                                          int srcW, int srcH, enum AVPixelFormat srcFormat,
                                          int dstW, int dstH, enum AVPixelFormat dstFormat,
                                          int flags, SwsFilter *srcFilter,
                                          SwsFilter *dstFilter, const double *param);
	int av_image_fill_arrays(uint8_t *dst_data[4], int dst_linesize[4],
													 const uint8_t *src,
													 enum AVPixelFormat pix_fmt, int width, int height, int align);
  int av_frame_get_buffer(AVFrame *frame, int align);
  int avio_open(AVIOContext **s, const char *url, int flags);
}

class VideoWriter {
public:
  VideoWriter(string _filename, int _fps, int _width, int _height,
    AVPixelFormat _inPixFmt, AVPixelFormat _outPixFmt, AVCodecID _outCodecId,
    vector<string> opts);
  ~VideoWriter();
  bool AddFrame(uint8_t* img, int linesize, uint64_t timestamp);
  void Close();

private:
  AVStream* ConfigureVideoStream(AVFormatContext* oc, AVCodecID codec_id,
    int width, int height, int bitrate, int fps, AVPixelFormat codecPixFmt);
  AVFrame* AllocateFrame(AVPixelFormat pixFmt,int width,int height,bool alloc);


  AVOutputFormat* fmt;
  AVFormatContext* oc; // formerly, fc
  AVStream* outStream; // video stream, has its own AVCodecContext, is video_st

  AVFrame *bgrPic, *yuvPic;
  AVPacket pkt;

  AVCodecID outCodecId;
  AVPixelFormat inPixFmt, outPixFmt;

  struct SwsContext* sws_context;

  string filename;
  int fps, width, height, bitrate, frameNum;
  bool ok;
};

// Tested with avi filenames.
class DepthVideoWriter : public VideoWriter {
public:
  DepthVideoWriter(string _filename, int _fps, int _width, int _height,
      int nThreads=4, vector<string> opts={"coder", "1", "level", "3",
      "threads", to_string(4), "pass", "1"}) : VideoWriter(_filename,
        _fps, _width, _height, AV_PIX_FMT_GRAY16, AV_PIX_FMT_GRAY16,
        AV_CODEC_ID_FFV1, opts)
      {};

  bool AddFrame(cv::Mat img, uint64_t timestamp) {
    return VideoWriter::AddFrame((uint8_t*) img.data, img.step, timestamp);
  }
};

class BgrVideoWriter : public VideoWriter {
public:
  BgrVideoWriter(string _filename, int _fps, int _width, int _height,
      vector<string> opts={"preset", "medium", "tune", "film", "crf", "18",
      "pixel_format", "yuv420p"}) : VideoWriter(_filename, _fps, _width,
        _height, AV_PIX_FMT_BGR32, AV_PIX_FMT_YUV420P, AV_CODEC_ID_H264, opts)
      {};
  // BgrVideoWriter(string _filename, int _fps, int _width, int _height,
  //     vector<string> opts={"profile", "high", "level", "4.1"}) :
  //     VideoWriter(_filename, _fps, _width,
  //       _height, AV_PIX_FMT_BGR24, AV_PIX_FMT_YUV420P, AV_CODEC_ID_H264, opts)
  //     {};

  bool AddFrame(cv::Mat img, uint64_t timestamp) {
    return VideoWriter::AddFrame((uint8_t*) img.data, img.step, timestamp);
  }
};

class RgbVideoWriter : public VideoWriter {
public:
  RgbVideoWriter(string _filename, int _fps, int _width, int _height,
      vector<string> opts={"preset", "medium", "tune", "film", "crf", "18", "pixel_format",
        "yuv420p"}) : VideoWriter(_filename, _fps, _width, _height, AV_PIX_FMT_RGB32,
        AV_PIX_FMT_YUV420P, AV_CODEC_ID_H264, opts)
        {};

  bool AddFrame(cv::Mat img, uint64_t timestamp) {
    return VideoWriter::AddFrame((uint8_t*) img.data, img.step, timestamp);
  }
};
