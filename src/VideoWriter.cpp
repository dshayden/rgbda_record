#include <opencv2/opencv.hpp>

#include "VideoWriter.hpp"

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


// #define _AV_TS_MAX_STRING_SIZE 32
// #define _av_ts2str(ts) _av_ts_make_string((char[_AV_TS_MAX_STRING_SIZE]){0}, ts)
// #define _av_ts2timestr(ts, tb) _av_ts_make_time_string((char[_AV_TS_MAX_STRING_SIZE]){0}, ts, tb)
//
// static inline char* _av_ts_make_string(char *buf, int64_t ts)
// {
//     if (ts == AV_NOPTS_VALUE) snprintf(buf, _AV_TS_MAX_STRING_SIZE, "NOPTS");
//     else                      snprintf(buf, _AV_TS_MAX_STRING_SIZE, "%" PRId64, ts);
//     return buf;
// }
//
// static inline char* _av_ts_make_time_string(char *buf, int64_t ts, AVRational *tb) {
//   if (ts == AV_NOPTS_VALUE) snprintf(buf, _AV_TS_MAX_STRING_SIZE, "NOPTS");
//   else                      snprintf(buf, _AV_TS_MAX_STRING_SIZE, "%.6g", av_q2d(*tb) * ts);
//   return buf;
// }
//
//
// static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt) {
//   AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
//
//   printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
//     _av_ts2str(pkt->pts), _av_ts2timestr(pkt->pts, time_base),
//     _av_ts2str(pkt->dts), _av_ts2timestr(pkt->dts, time_base),
//     _av_ts2str(pkt->duration), _av_ts2timestr(pkt->duration, time_base),
//     pkt->stream_index);
// }

void VideoWriter::Close() {
  if (!ok) return;
  int ret, gotOutput;
  AVCodecContext* c = outStream->codec;

  do {
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    ret = avcodec_encode_video2(c, &pkt, NULL, &gotOutput);

    if (ret < 0) {
      cout << "error in avcodec_encode_video2, #: " << ret << endl;
      break;
    }

    if (gotOutput) {
      // pkt.dts = frameNum++;
      // std::cout << "pts, dts: " << pkt.pts << ", " << pkt.dts << std::endl;

      if (pkt.pts != (int64_t)AV_NOPTS_VALUE) {
        pkt.pts = av_rescale_q(pkt.pts, c->time_base, outStream->time_base);
      }
      if (pkt.dts != (int64_t)AV_NOPTS_VALUE) {
        pkt.dts = av_rescale_q(pkt.dts, c->time_base, outStream->time_base);
      }
      // pkt.duration = 0;

      if (pkt.duration) { // todo: need to figure out how to set duration
        pkt.duration = av_rescale_q(pkt.duration, c->time_base,
          outStream->time_base);
      }

      pkt.stream_index = outStream->index;

      // ret = av_write_frame(oc, &pkt);
      ret = av_interleaved_write_frame(oc, &pkt);
      if (ret < 0) cout << "error in av_write_frame, #: " << ret << endl;

      av_packet_unref(&pkt);
    }
  } while (gotOutput);

  avio_flush(oc->pb);

  ret = av_write_trailer(oc);

  if (bgrPic) av_free(bgrPic);
  if (yuvPic) av_free(yuvPic);

  ret = avcodec_close(outStream->codec);
  ret = avio_close(oc->pb);
  avformat_free_context(oc);

  oc = NULL;
  ok = false;
}

bool VideoWriter::AddFrame(uint8_t* img, int linesize, uint64_t timestamp) {
  int ret;

  AVCodecContext* c = outStream->codec;

  // Convert
  // (don't need to make this extraneous copy, the sws_scale already will)
  // ret = av_image_fill_arrays(bgrPic->data, bgrPic->linesize, img,
  //   inPixFmt, width, height, 1);
  // if (ret <= 0) cout << "error in av_image_fill_arrays, #: " << ret << endl;

  ret = av_frame_get_buffer(yuvPic, 1);
  if (ret < 0) cout << "error in av_frame_get_buffer, #: " << ret << endl;

  sws_context = sws_getCachedContext(sws_context, c->width, c->height, inPixFmt,
    c->width, c->height, outPixFmt, 0, 0, 0, 0);
  sws_scale(sws_context, &img, &linesize, 0, c->height,
    yuvPic->data, yuvPic->linesize);

  // sws_scale(sws_context, bgrPic->data, bgrPic->linesize, 0, c->height,
  //   yuvPic->data, yuvPic->linesize);

  // yuvPic->pts = timestamp;
  yuvPic->pts = frameNum++; // TODO: handle pts here
  // // yuvPic->pts += av_rescale_q(1, outStream->codec->time_base, outStream->time_base);

  // Encode
  AVPacket pkt;
  av_init_packet(&pkt);
  pkt.data = NULL;
  pkt.size = 0;

  int gotOutput = 0;
  ret = avcodec_encode_video2(c, &pkt, yuvPic, &gotOutput);

  if (ret < 0) {
    cout << "error in avcodec_encode_video2, #: " << ret << endl;
    return false;
  }

  if (gotOutput) {
    // pkt.dts = frameNum++;
    // std::cout << "pts, dts: " << pkt.pts << ", " << pkt.dts << std::endl;

    if (pkt.pts != (int64_t)AV_NOPTS_VALUE) {
      uint64_t old = pkt.pts;
      pkt.pts = av_rescale_q(pkt.pts, c->time_base, outStream->time_base);
      // std::cout << "pts (new, old): " << pkt.pts << ", " << old << std::endl;
    }
    if (pkt.dts != (int64_t)AV_NOPTS_VALUE) {
      uint64_t old = pkt.dts;
      pkt.dts = av_rescale_q(pkt.dts, c->time_base, outStream->time_base);
      // std::cout << "dts (new, old): " << pkt.dts << ", " << old << std::endl;
    }

    // pkt.duration = 33;
    // pkt.duration = 0;

    if (pkt.duration) { // todo: need to figure out how to set duration?
      pkt.duration = av_rescale_q(pkt.duration, c->time_base,
        outStream->time_base);
    }
    pkt.stream_index = outStream->index;

    // todo: choose between write_frame and interleaved_write_frame. do for here
    //       and Close()
    // ret = av_write_frame(oc, &pkt);
    ret = av_interleaved_write_frame(oc, &pkt);
    if (ret < 0) cout << "error in av_write_frame, #: " << ret << endl;

    av_packet_unref(&pkt);
  }

  if (ret >= 0) return true;
  else return false;
}

VideoWriter::VideoWriter(string _filename, int _fps, int _width, int _height,
  AVPixelFormat _inPixFmt, AVPixelFormat _outPixFmt, AVCodecID _outCodecId,
  vector<string> opts) :
    filename(_filename), fps(_fps), width(_width), height(_height),
    inPixFmt(_inPixFmt), outPixFmt(_outPixFmt), outCodecId(_outCodecId)
{
  ok = false;

  avcodec_register_all();
  av_register_all();

  fmt = av_guess_format(NULL, filename.c_str(), NULL);
  if (!fmt) cerr << "Could not guess format, invalid state\n";
  
  oc = avformat_alloc_context();
  if (!oc) cerr << "AVFormatContext not initialized, invalid state\n";
  oc->oformat = fmt;
  snprintf(oc->filename, sizeof(oc->filename), "%s", filename.c_str());
  oc->max_delay = (int)(0.7*AV_TIME_BASE); // reduce buffer underrun warnings according to OpenCV

  double bitrate_scale = 1;
  bitrate = MIN(bitrate_scale*fps*width*height, (double)INT_MAX/2);

  outStream = ConfigureVideoStream(oc, outCodecId, width, height,
      (int)(bitrate + 0.5), fps, outPixFmt);
  if (!outStream) {cerr << "AVStream not configured, invalid state.\n";}
  
  AVCodecContext* c = outStream->codec;
  AVCodec* codec = avcodec_find_encoder(c->codec_id);
  if (!codec) cerr << "Could not find AVStream codec, invalid state.\n";

	int64_t lbit_rate = (int64_t)c->bit_rate;
	lbit_rate += (bitrate / 2);
	lbit_rate = std::min(lbit_rate, (int64_t)INT_MAX);
	c->bit_rate_tolerance = (int)lbit_rate;
	c->bit_rate = (int)lbit_rate;
  
  AVDictionary * codec_options( 0 );
  for (int i=0; i<opts.size()-1; i+=2) {
    av_dict_set(&codec_options, opts[i].c_str(), opts[i+1].c_str(), 0);
  }

  // av_dict_set( &codec_options, "coder", "1", 0 ); // allow gray16
  // av_dict_set( &codec_options, "level", "3", 0 );
  // av_dict_set( &codec_options, "threads", "4", 0 );
  // av_dict_set( &codec_options, "pass", "1", 0 );

  // h264 encoding parameters
  // av_dict_set( &codec_options, "preset", "ultrafast", 0 );
  // av_dict_set( &codec_options, "tune", "film", 0 );
  // av_dict_set( &codec_options, "crf", "23", 0 );
  // av_dict_set( &codec_options, "pixel_format", "yuv420p", 0 );

  int err = avcodec_open2(c, codec, &codec_options);
  // int err = avcodec_open2(c, codec, NULL);
  if (err < 0) cerr << "Error: avcodec_open2 failed.\n";

  // Allocate bgrPic, yuvPic
  bool needColorConvert = false;
  bgrPic = AllocateFrame(inPixFmt, c->width, c->height, needColorConvert); // same as input_picture
  yuvPic = AllocateFrame(c->pix_fmt, c->width, c->height, needColorConvert);
  if (!bgrPic || !yuvPic)
    cerr << "Could not allocate bgr or yuv frames, invalid state.\n";
  
  err = avio_open(&oc->pb, filename.c_str(), AVIO_FLAG_WRITE);
  if (err < 0) cerr << "Could not open output file, invalid state.\n";

  err = avformat_write_header(oc, NULL);
  if (err < 0) {
    //close();
  }

  frameNum = 0;
  // frameNum = 51;
  ok = true;

  sws_context = NULL;
}

AVStream* VideoWriter::ConfigureVideoStream(AVFormatContext* oc,
  AVCodecID codec_id, int width, int height, int bitrate, int fps,
  AVPixelFormat codecPixFmt)
{
  AVStream* st = avformat_new_stream(oc, 0);
  if (!st) {cerr << "Could not add stream!\n"; return NULL;}
  
  AVCodecContext* c = st->codec;
  c->codec_id = codec_id;

  AVCodec *codec = avcodec_find_encoder(c->codec_id);
  if (!codec) cerr << "Could not find encoder with codec_id: " << c->codec_id << endl;
  c->codec_type = AVMEDIA_TYPE_VIDEO;
  avcodec_get_context_defaults3(c, codec);
  c->codec_id = codec_id; // Deletes codec_id field, apparently.

  // Sample parameters
	// int64_t lbit_rate = (int64_t)bitrate;
	// lbit_rate += (bitrate / 2);
	// lbit_rate = std::min(lbit_rate, (int64_t)INT_MAX);
	// c->bit_rate = lbit_rate;
	
  c->width = width;
  c->height = height;

  // Timebase
  int frame_rate = (int)(fps+0.5);
  int frame_rate_base = 1;
	while (fabs((double)frame_rate/frame_rate_base) - fps > 0.001){
		frame_rate_base*=10;
		frame_rate=(int)(fps*frame_rate_base + 0.5);
	}
  st->time_base.den = frame_rate;
  st->time_base.num = frame_rate_base;

  c->time_base.den = frame_rate;
  c->time_base.num = frame_rate_base;

  // printf("den: %d, num: %d\n", frame_rate, frame_rate_base);

  // int denom = 1000;
  // st->time_base = {1, denom};
  // c->time_base = {1, denom};

  c->pix_fmt = codecPixFmt;

  // Allow presets system to handle defaults
  c->gop_size = -1;
  c->qmin = -1;
  // c->qmin = 2; // for mjpeg, which is slower (!)
  // c->qmax = 2;
  c->bit_rate = 0;

  // if (c->priv_data) av_opt_set(c->priv_data,"crf","23", 0);
  // if (c->priv_data) av_opt_set(c->priv_data,"crf","25", 0);

  // consider also preset, maxrate, bufsize

  if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
    c->flags |= CODEC_FLAG_GLOBAL_HEADER;
  }

  return st;
}

AVFrame* VideoWriter::AllocateFrame(AVPixelFormat pixFmt, int width,
  int height, bool alloc)
{
  AVFrame * picture = av_frame_alloc();
  if (!picture) return NULL;
  picture->format = pixFmt;
  picture->width = width;
  picture->height = height;

  return picture;
}

VideoWriter::~VideoWriter() {
  Close();
}
