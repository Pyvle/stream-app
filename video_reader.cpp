#include "video_reader.hpp"

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
		return false;
	}

	stream_index = -1;
	AVCodecParameters* av_codec_params;
	const AVCodec* av_codec;
	for (int i = 0;i < av_format_ctx->nb_streams;i++) {
		av_codec_params = av_format_ctx->streams[i]->codecpar;
		av_codec = avcodec_find_decoder(av_codec_params->codec_id);
		if (!av_codec) {
			continue;
		}
		if (av_codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
			stream_index = i;
			width = av_codec_params->width;
			height = av_codec_params->height;
			time_base = av_format_ctx->streams[i]->time_base;
			break;
		}
	}

	int a = 1;

	if (stream_index == -1) {
		printf("Couldn't find valid video stream inside file\n");
		return false;
	}

	av_codec_ctx = avcodec_alloc_context3(av_codec);
	if (!av_codec_ctx) {
		printf("Couldn't create AVCodecContext\n");
		return false;
	}
	if (avcodec_parameters_to_context(av_codec_ctx, av_codec_params) < 0) {
		printf("Couldn't initialize AVCodecContext\n");
		return false;
	}
	if (avcodec_open2(av_codec_ctx, av_codec, NULL) < 0) {
		printf("Couldn't open codec");
		return false;
	}

	av_frame = av_frame_alloc();
	if (!av_frame) {
		printf("Couldn't allocate AVFrame");
		return false;
	}
	av_packet = av_packet_alloc();
	if (!av_packet) {
		printf("Couldn't allocate AVPacket");
		return false;
	}

	return true;
}

bool video_reader_read(VideoReaderState* state, uint8_t* buffer, int64_t* pts)
{
	auto& width = state->width;
	auto& height = state->height;
	auto& time_base = state->time_base;
	auto& stream_index = state->stream_index;
	auto& av_format_ctx = state->av_format_ctx;
	auto& av_codec_ctx = state->av_codec_ctx;
	auto& av_frame = state->av_frame;
	auto& av_packet = state->av_packet;
	auto& sws_scaler_ctx = state->sws_scaler_ctx;

	int response;
	while (av_read_frame(av_format_ctx, av_packet) >= 0) {
		if (av_packet->stream_index != stream_index) {
			av_packet_unref(av_packet);
			continue;
		}

		response = avcodec_send_packet(av_codec_ctx, av_packet);
		if (response < 0) {
			printf("Failed to decode packet");
			return 1;
		}
		response = avcodec_receive_frame(av_codec_ctx, av_frame);
		if (response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
			av_packet_unref(av_packet);
			continue;
		}
		if (response < 0) {
			printf("Failed to decode packet");
			return false;
		}

		av_packet_unref(av_packet);
		break;
	}

	*pts = av_frame->pts;

	sws_scaler_ctx = sws_getContext(width, height, av_codec_ctx->pix_fmt,
		width, height, AV_PIX_FMT_RGB0,
		SWS_BILINEAR, 0, 0, 0);
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
	sws_freeContext(state->sws_scaler_ctx);
	avformat_close_input(&state->av_format_ctx);
	avformat_free_context(state->av_format_ctx);
	av_frame_free(&state->av_frame);
	av_packet_free(&state->av_packet);
	avcodec_free_context(&state->av_codec_ctx);

	return true;
}

