#pragma once

#include <thread>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

struct VideoReaderState {
	int width, height;
	AVRational time_base;

	int stream_index;
	AVFormatContext* av_format_ctx;
	AVCodecContext* av_codec_ctx;
	AVFrame* av_frame;
	AVPacket* av_packet;
	SwsContext* sws_scaler_ctx;
};

bool video_reader_open(VideoReaderState* state, const char* filename);
bool video_reader_read(VideoReaderState* state, uint8_t*& buffer, int64_t* pts);
bool video_reader_close(VideoReaderState* state);