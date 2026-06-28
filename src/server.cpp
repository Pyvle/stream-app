#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

struct ServerOptions {
	std::string source = "desktop";
	std::string output_url = "udp://127.0.0.1:9000";
	int fps = 30;
	int bitrate = 800000;
};

enum class ParseResult {
	Ok,
	Help,
	Error
};

static void print_usage() {
	printf("Usage: server [options]\n");
	printf("Options:\n");
	printf("  --source, -s <input>    Input source, defaults to Windows desktop capture\n");
	printf("  --output, -o <url>      FFmpeg output URL, defaults to udp://127.0.0.1:9000\n");
	printf("  --host <host>           UDP host shortcut, defaults to 127.0.0.1\n");
	printf("  --port <port>           UDP port shortcut, defaults to 9000\n");
	printf("  --fps <value>           Capture frame rate, defaults to 30\n");
	printf("  --bitrate <value>       Encoder bitrate in bit/s, defaults to 800000\n");
}

static bool read_option_value(int& index, int argc, char* argv[], std::string& value) {
	if (index + 1 >= argc) {
		return false;
	}

	value = argv[++index];
	return true;
}

static int read_positive_int(const std::string& value, int fallback) {
	const int parsed = std::atoi(value.c_str());
	return parsed > 0 ? parsed : fallback;
}

static ParseResult parse_args(int argc, char* argv[], ServerOptions& options) {
	std::string host = "127.0.0.1";
	std::string port = "9000";
	bool output_overridden = false;
	bool endpoint_overridden = false;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		std::string value;

		if (arg == "--help" || arg == "-h") {
			print_usage();
			return ParseResult::Help;
		}
		else if ((arg == "--source" || arg == "-s") && read_option_value(i, argc, argv, value)) {
			options.source = value;
		}
		else if ((arg == "--output" || arg == "-o") && read_option_value(i, argc, argv, value)) {
			options.output_url = value;
			output_overridden = true;
		}
		else if (arg == "--host" && read_option_value(i, argc, argv, value)) {
			host = value;
			endpoint_overridden = true;
		}
		else if (arg == "--port" && read_option_value(i, argc, argv, value)) {
			port = value;
			endpoint_overridden = true;
		}
		else if (arg == "--fps" && read_option_value(i, argc, argv, value)) {
			options.fps = read_positive_int(value, options.fps);
		}
		else if (arg == "--bitrate" && read_option_value(i, argc, argv, value)) {
			options.bitrate = read_positive_int(value, options.bitrate);
		}
		else if (arg.rfind("--source=", 0) == 0) {
			options.source = arg.substr(9);
		}
		else if (arg.rfind("--output=", 0) == 0) {
			options.output_url = arg.substr(9);
			output_overridden = true;
		}
		else if (arg.rfind("--host=", 0) == 0) {
			host = arg.substr(7);
			endpoint_overridden = true;
		}
		else if (arg.rfind("--port=", 0) == 0) {
			port = arg.substr(7);
			endpoint_overridden = true;
		}
		else if (arg.rfind("--fps=", 0) == 0) {
			options.fps = read_positive_int(arg.substr(6), options.fps);
		}
		else if (arg.rfind("--bitrate=", 0) == 0) {
			options.bitrate = read_positive_int(arg.substr(10), options.bitrate);
		}
		else {
			printf("Unknown option: %s\n", arg.c_str());
			print_usage();
			return ParseResult::Error;
		}
	}

	if (endpoint_overridden && !output_overridden) {
		options.output_url = "udp://" + host + ":" + port;
	}

	options.fps = std::max(options.fps, 1);
	options.bitrate = std::max(options.bitrate, 1);
	return ParseResult::Ok;
}

static void print_ffmpeg_error(const char* context, int error_code) {
	char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(error_code, buffer, sizeof(buffer));
	fprintf(stderr, "%s: %s\n", context, buffer);
}

int main(int argc, char* argv[]) {
	ServerOptions options;
	const ParseResult parse_result = parse_args(argc, argv, options);
	if (parse_result == ParseResult::Help) {
		return 0;
	}
	if (parse_result == ParseResult::Error) {
		return 1;
	}

	avdevice_register_all();
	avformat_network_init();

	int exit_code = 1;
	int ret = 0;
	AVDictionary* input_options = nullptr;
	AVDictionary* encoder_options = nullptr;
	AVFormatContext* input_ctx = nullptr;
	AVFormatContext* output_ctx = nullptr;
	AVCodecContext* dec_ctx = nullptr;
	AVCodecContext* enc_ctx = nullptr;
	SwsContext* sws_ctx = nullptr;
	AVFrame* dec_frame = nullptr;
	AVFrame* enc_frame = nullptr;
	AVPacket* in_pkt = nullptr;
	AVPacket* out_pkt = nullptr;
	int stream_index = -1;
	AVStream* in_stream = nullptr;
	AVStream* out_stream = nullptr;
	const AVCodec* decoder = nullptr;
	const AVCodec* encoder = nullptr;
	int64_t pts = 0;

	const AVInputFormat* input_format = nullptr;
	if (options.source == "desktop") {
		input_format = av_find_input_format("gdigrab");
		av_dict_set(&input_options, "framerate", std::to_string(options.fps).c_str(), 0);
	}

	ret = avformat_open_input(&input_ctx, options.source.c_str(), input_format, &input_options);
	if (ret < 0) {
		print_ffmpeg_error("Could not open input", ret);
		goto cleanup;
	}

	ret = avformat_find_stream_info(input_ctx, nullptr);
	if (ret < 0) {
		print_ffmpeg_error("Could not find stream info", ret);
		goto cleanup;
	}

	stream_index = av_find_best_stream(input_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (stream_index < 0) {
		print_ffmpeg_error("Could not find video stream", stream_index);
		goto cleanup;
	}

	in_stream = input_ctx->streams[stream_index];
	decoder = avcodec_find_decoder(in_stream->codecpar->codec_id);
	if (!decoder) {
		fprintf(stderr, "No decoder found for input video stream\n");
		goto cleanup;
	}

	dec_ctx = avcodec_alloc_context3(decoder);
	if (!dec_ctx) {
		fprintf(stderr, "Could not allocate decoder context\n");
		goto cleanup;
	}

	ret = avcodec_parameters_to_context(dec_ctx, in_stream->codecpar);
	if (ret < 0) {
		print_ffmpeg_error("Could not copy decoder parameters", ret);
		goto cleanup;
	}

	ret = avcodec_open2(dec_ctx, decoder, nullptr);
	if (ret < 0) {
		print_ffmpeg_error("Could not open decoder", ret);
		goto cleanup;
	}

	if (dec_ctx->width <= 0 || dec_ctx->height <= 0) {
		fprintf(stderr, "Input stream has invalid frame size\n");
		goto cleanup;
	}

	ret = avformat_alloc_output_context2(&output_ctx, nullptr, "mpegts", options.output_url.c_str());
	if (ret < 0 || !output_ctx) {
		print_ffmpeg_error("Could not create output context", ret);
		goto cleanup;
	}

	encoder = avcodec_find_encoder_by_name("libx264");
	if (!encoder) {
		encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
	}
	if (!encoder) {
		fprintf(stderr, "No H.264 encoder found\n");
		goto cleanup;
	}

	enc_ctx = avcodec_alloc_context3(encoder);
	if (!enc_ctx) {
		fprintf(stderr, "Could not allocate encoder context\n");
		goto cleanup;
	}

	enc_ctx->width = dec_ctx->width;
	enc_ctx->height = dec_ctx->height;
	enc_ctx->time_base = AVRational{ 1, options.fps };
	enc_ctx->framerate = AVRational{ options.fps, 1 };
	enc_ctx->gop_size = options.fps * 2;
	enc_ctx->max_b_frames = 0;
	enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
	enc_ctx->bit_rate = options.bitrate;
	if (output_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
		enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	av_dict_set(&encoder_options, "preset", "ultrafast", 0);
	av_dict_set(&encoder_options, "tune", "zerolatency", 0);

	ret = avcodec_open2(enc_ctx, encoder, &encoder_options);
	if (ret < 0) {
		print_ffmpeg_error("Could not open encoder", ret);
		goto cleanup;
	}

	out_stream = avformat_new_stream(output_ctx, nullptr);
	if (!out_stream) {
		fprintf(stderr, "Could not create output stream\n");
		goto cleanup;
	}

	ret = avcodec_parameters_from_context(out_stream->codecpar, enc_ctx);
	if (ret < 0) {
		print_ffmpeg_error("Could not copy encoder parameters", ret);
		goto cleanup;
	}
	out_stream->time_base = enc_ctx->time_base;

	if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open2(&output_ctx->pb, options.output_url.c_str(), AVIO_FLAG_WRITE, nullptr, nullptr);
		if (ret < 0) {
			print_ffmpeg_error("Could not open output", ret);
			goto cleanup;
		}
	}

	ret = avformat_write_header(output_ctx, nullptr);
	if (ret < 0) {
		print_ffmpeg_error("Could not write output header", ret);
		goto cleanup;
	}

	sws_ctx = sws_getContext(
		dec_ctx->width,
		dec_ctx->height,
		dec_ctx->pix_fmt,
		enc_ctx->width,
		enc_ctx->height,
		enc_ctx->pix_fmt,
		SWS_BILINEAR,
		nullptr,
		nullptr,
		nullptr);
	if (!sws_ctx) {
		fprintf(stderr, "Could not create scaler context\n");
		goto cleanup;
	}

	dec_frame = av_frame_alloc();
	enc_frame = av_frame_alloc();
	in_pkt = av_packet_alloc();
	out_pkt = av_packet_alloc();
	if (!dec_frame || !enc_frame || !in_pkt || !out_pkt) {
		fprintf(stderr, "Could not allocate frame or packet buffers\n");
		goto cleanup;
	}

	enc_frame->format = enc_ctx->pix_fmt;
	enc_frame->width = enc_ctx->width;
	enc_frame->height = enc_ctx->height;
	ret = av_frame_get_buffer(enc_frame, 32);
	if (ret < 0) {
		print_ffmpeg_error("Could not allocate encoder frame buffer", ret);
		goto cleanup;
	}

	while ((ret = av_read_frame(input_ctx, in_pkt)) >= 0) {
		if (in_pkt->stream_index != stream_index) {
			av_packet_unref(in_pkt);
			continue;
		}

		ret = avcodec_send_packet(dec_ctx, in_pkt);
		av_packet_unref(in_pkt);
		if (ret < 0) {
			print_ffmpeg_error("Could not send packet to decoder", ret);
			continue;
		}

		while ((ret = avcodec_receive_frame(dec_ctx, dec_frame)) == 0) {
			ret = av_frame_make_writable(enc_frame);
			if (ret < 0) {
				print_ffmpeg_error("Could not make encoder frame writable", ret);
				goto cleanup;
			}

			sws_scale(
				sws_ctx,
				dec_frame->data,
				dec_frame->linesize,
				0,
				dec_ctx->height,
				enc_frame->data,
				enc_frame->linesize);

			enc_frame->pts = pts++;

			ret = avcodec_send_frame(enc_ctx, enc_frame);
			if (ret < 0) {
				print_ffmpeg_error("Could not send frame to encoder", ret);
				goto cleanup;
			}

			while ((ret = avcodec_receive_packet(enc_ctx, out_pkt)) == 0) {
				out_pkt->stream_index = out_stream->index;
				av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_stream->time_base);
				ret = av_interleaved_write_frame(output_ctx, out_pkt);
				av_packet_unref(out_pkt);
				if (ret < 0) {
					print_ffmpeg_error("Could not write encoded packet", ret);
					goto cleanup;
				}
			}

			if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
				print_ffmpeg_error("Could not receive encoded packet", ret);
				goto cleanup;
			}
		}

		if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
			print_ffmpeg_error("Could not receive decoded frame", ret);
			goto cleanup;
		}
	}

	ret = avcodec_send_frame(enc_ctx, nullptr);
	if (ret >= 0) {
		while ((ret = avcodec_receive_packet(enc_ctx, out_pkt)) == 0) {
			out_pkt->stream_index = out_stream->index;
			av_packet_rescale_ts(out_pkt, enc_ctx->time_base, out_stream->time_base);
			const int write_ret = av_interleaved_write_frame(output_ctx, out_pkt);
			av_packet_unref(out_pkt);
			if (write_ret < 0) {
				print_ffmpeg_error("Could not write flushed packet", write_ret);
				goto cleanup;
			}
		}
	}

	av_write_trailer(output_ctx);
	exit_code = 0;

cleanup:
	av_dict_free(&input_options);
	av_dict_free(&encoder_options);
	av_packet_free(&out_pkt);
	av_packet_free(&in_pkt);
	av_frame_free(&enc_frame);
	av_frame_free(&dec_frame);
	sws_freeContext(sws_ctx);
	avcodec_free_context(&enc_ctx);
	avcodec_free_context(&dec_ctx);
	avformat_close_input(&input_ctx);
	if (output_ctx && !(output_ctx->oformat->flags & AVFMT_NOFILE)) {
		avio_closep(&output_ctx->pb);
	}
	avformat_free_context(output_ctx);

	return exit_code;
}
