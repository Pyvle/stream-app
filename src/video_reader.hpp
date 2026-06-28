#pragma once

#include <cstdint>
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
	int width = 0;
	int height = 0;
	AVRational time_base = { 0, 1 };

	int stream_index = -1;
	AVFormatContext* av_format_ctx = nullptr;
	AVCodecContext* av_codec_ctx = nullptr;
	AVFrame* av_frame = nullptr;
	AVPacket* av_packet = nullptr;
	SwsContext* sws_scaler_ctx = nullptr;
};

bool video_reader_open(VideoReaderState* state, const char* filename);
bool video_reader_read(VideoReaderState* state, uint8_t*& buffer, int64_t* pts);
bool video_reader_close(VideoReaderState* state);
