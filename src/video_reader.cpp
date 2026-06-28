#include "video_reader.hpp"

#include <new>

bool video_reader_open(VideoReaderState* state, const char* filename)
{
	avformat_network_init();

	auto& width = state->width;
	auto& height = state->height;
	auto& time_base = state->time_base;
	auto& stream_index = state->stream_index;
	auto& av_format_ctx = state->av_format_ctx;
	auto& av_codec_ctx = state->av_codec_ctx;
	auto& av_packet = state->av_packet;
	auto& av_frame = state->av_frame;

	av_format_ctx = avformat_alloc_context();
	if (!av_format_ctx) {
		printf("Couldn't create AVFormatContext\n");
		return false;
	}

	if (avformat_open_input(&av_format_ctx, filename, NULL, NULL) < 0) {
		printf("Could not open video file\n");
		avformat_free_context(av_format_ctx);
		av_format_ctx = nullptr;
		return false;
	}

	if (avformat_find_stream_info(av_format_ctx, NULL) < 0) {
		printf("Couldn't find stream info\n");
		avformat_close_input(&av_format_ctx);
		return false;
	}

	stream_index = -1;
	AVCodecParameters* av_codec_params = nullptr;
	const AVCodec* av_codec = nullptr;
	for (unsigned int i = 0; i < av_format_ctx->nb_streams; i++) {
		av_codec_params = av_format_ctx->streams[i]->codecpar;
		av_codec = avcodec_find_decoder(av_codec_params->codec_id);
		if (!av_codec) {
			continue;
		}
		if (av_codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
			stream_index = i;
			time_base = av_format_ctx->streams[i]->time_base;
			break;
		}
	}

	if (stream_index == -1) {
		printf("Couldn't find valid video stream inside file\n");
		avformat_close_input(&av_format_ctx);
		return false;
	}

	av_codec_ctx = avcodec_alloc_context3(av_codec);
	if (!av_codec_ctx) {
		printf("Couldn't create AVCodecContext\n");
		avformat_close_input(&av_format_ctx);
		return false;
	}
	if (avcodec_parameters_to_context(av_codec_ctx, av_codec_params) < 0) {
		printf("Couldn't initialize AVCodecContext\n");
		video_reader_close(state);
		return false;
	}
	if (avcodec_open2(av_codec_ctx, av_codec, NULL) < 0) {
		printf("Couldn't open codec");
		video_reader_close(state);
		return false;
	}

	av_frame = av_frame_alloc();
	if (!av_frame) {
		printf("Couldn't allocate AVFrame");
		video_reader_close(state);
		return false;
	}
	av_packet = av_packet_alloc();
	if (!av_packet) {
		printf("Couldn't allocate AVPacket");
		video_reader_close(state);
		return false;
	}

	return true;
}

bool video_reader_read(VideoReaderState* state, uint8_t*& buffer, int64_t* pts)
{
	auto& width = state->width;
	auto& height = state->height;
	auto& stream_index = state->stream_index;
	auto& av_format_ctx = state->av_format_ctx;
	auto& av_codec_ctx = state->av_codec_ctx;
	auto& av_frame = state->av_frame;
	auto& av_packet = state->av_packet;
	auto& sws_scaler_ctx = state->sws_scaler_ctx;

	int response;
	bool frame_received = false;
	while (av_read_frame(av_format_ctx, av_packet) >= 0) {
		if (av_packet->stream_index != stream_index) {
			av_packet_unref(av_packet);
			continue;
		}

		response = avcodec_send_packet(av_codec_ctx, av_packet);
		av_packet_unref(av_packet);
		if (response < 0) {
			printf("Failed to decode packet");
			return false;
		}
		response = avcodec_receive_frame(av_codec_ctx, av_frame);
		if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
			continue;
		}
		if (response < 0) {
			printf("Failed to decode packet");
			return false;
		}

		frame_received = true;
		break;
	}

	if (!frame_received) {
		return false;
	}

	if (width != av_frame->width || height != av_frame->height || buffer == nullptr) {
		delete[] buffer;
		width = av_frame->width;
		height = av_frame->height;
		buffer = new (std::nothrow) uint8_t[width * height * 4];
		if (!buffer) {
			printf("Couldn't allocate frame buffer\n");
			return false;
		}
	}

	*pts = av_frame->pts;

	sws_scaler_ctx = sws_getCachedContext(
		sws_scaler_ctx,
		av_frame->width,
		av_frame->height,
		av_codec_ctx->pix_fmt,
		width,
		height,
		AV_PIX_FMT_RGBA,
		SWS_BILINEAR,
		nullptr,
		nullptr,
		nullptr);
	if (!sws_scaler_ctx) {
		printf("Couldn't initialize sw_scaler\n");
		return false;
	}

	uint8_t* dest[4] = { buffer, NULL, NULL, NULL };
	int dest_linesize[4] = { width * 4, 0, 0, 0 };
	sws_scale(sws_scaler_ctx, av_frame->data, av_frame->linesize, 0, av_frame->height, dest, dest_linesize);

	return true;
}

bool video_reader_close(VideoReaderState* state)
{
	if (!state) {
		return true;
	}

	sws_freeContext(state->sws_scaler_ctx);
	state->sws_scaler_ctx = nullptr;
	av_frame_free(&state->av_frame);
	av_packet_free(&state->av_packet);
	avcodec_free_context(&state->av_codec_ctx);
	avformat_close_input(&state->av_format_ctx);

	return true;
}

