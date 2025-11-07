#include <stdio.h>
#include <string>
#include <chrono>
#include <thread>

#include <boost/asio.hpp>

#include "rtc/rtc.hpp"
#include <nlohmann/json.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}

using boost::asio::ip::udp;

static void send_packet(const AVPacket* av_packet, udp::socket& socket, udp::endpoint& remote_endpoint) {
	boost::system::error_code ec;
	socket.send_to(boost::asio::buffer(av_packet->data, av_packet->size), remote_endpoint, 0, ec);
	if (ec) {
		std::cerr << "Error: \"send to addr\"\n";
	}
}

int main(int argc, char* argv[]) {
	avdevice_register_all();
	avformat_network_init();

	AVDictionary* dict = nullptr;
	av_dict_set(&dict, "framerate", "30", 0);
	const AVInputFormat* i_format = av_find_input_format("gdigrab");
	AVFormatContext* i_av_format_ctx = avformat_alloc_context();

	/*if (avformat_open_input(&i_av_format_ctx, "C:/Users/geenb/Desktop/Rema.mp4", i_format, nullptr) != 0) {
		printf("Could not open video\n");
		return 1;
	}*/
	if (avformat_open_input(&i_av_format_ctx, "desktop", i_format, &dict) != 0) {
		printf("Could not open video\n");
		return 1;
	}
	av_dict_free(&dict);

	if (avformat_find_stream_info(i_av_format_ctx, NULL) < 0) {
		printf("Couldn't find info of stream\n");
		return 1;
	}
	if (!i_av_format_ctx) {
		printf("Could not init AVFormatContext\n");
		return 1;
	}

	AVFormatContext* o_av_format_ctx = nullptr;
	avformat_alloc_output_context2(&o_av_format_ctx, nullptr, "mpegts", "udp://127.0.0.1:9000");
	if (!o_av_format_ctx) {
		printf("Couldn't create output context\n");
	}

	if (!(o_av_format_ctx->oformat->flags & AVFMT_NOFILE)) {
		if (avio_open2(&o_av_format_ctx->pb, o_av_format_ctx->url, AVIO_FLAG_WRITE, nullptr, nullptr)) {
			printf("Couldn't open output io context\n");
			return 1;
		}
	}

	int stream_index = -1;
	stream_index = av_find_best_stream(i_av_format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (stream_index == -1) {
		printf("Could not found video stream\n");
		return 1;
	}

	AVStream* in_av_stream = i_av_format_ctx->streams[stream_index];

	int width = in_av_stream->codecpar->width;
	int height = in_av_stream->codecpar->height;
	AVPixelFormat in_pix_fmt = (AVPixelFormat)in_av_stream->codecpar->format;

	//find encoder
	const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
	if (!encoder) encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!encoder) {
		fprintf(stderr, "No H264 encoder found\n");
		return 1;
	}

	//Создать и настроить контекст энкодера
	AVCodecContext* enc_ctx = avcodec_alloc_context3(encoder);
	enc_ctx->width = width;
	enc_ctx->height = height;
	enc_ctx->time_base = AVRational{ 1,30 }; // 30fps
	enc_ctx->framerate = AVRational{ 30,1 };
	enc_ctx->gop_size = 60;
	enc_ctx->max_b_frames = 0;
	enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
	enc_ctx->bit_rate = 800000; // 800 kb/s

	//Опции
	AVDictionary* opts = nullptr;
	av_dict_set(&opts, "preset", "ultrafast", 0);
	av_dict_set(&opts, "tune", "zerolatency", 0);

	//Открыть энкодер
	int ret = avcodec_open2(enc_ctx, encoder, &opts);
	if (ret < 0) { char buf[256]; av_strerror(ret, buf, sizeof(buf)); fprintf(stderr, "open encoder: %s\n", buf); return 1; }

	//Создать выходный поток и присвоить параметры из контекста энкодера
	AVStream* out_av_stream = avformat_new_stream(o_av_format_ctx, nullptr);
	ret = avcodec_parameters_from_context(out_av_stream->codecpar, enc_ctx);
	if (ret < 0) { char buf[256]; av_strerror(ret, buf, sizeof(buf)); fprintf(stderr, "params from ctx: %s\n", buf); return 1; }

	//Записать заголовок
	ret = avformat_write_header(o_av_format_ctx, nullptr);
	if (ret < 0) { char buf[256]; av_strerror(ret, buf, sizeof(buf)); fprintf(stderr, "write header: %s\n", buf); return 1; }

	//Подготовить структуры для декодирования и для конвертации в YUV
	//Найти декодер и контекст для in stream
	const AVCodec* in_decoder = avcodec_find_decoder(in_av_stream->codecpar->codec_id);
	AVCodecContext* dec_ctx = avcodec_alloc_context3(in_decoder);
	avcodec_parameters_to_context(dec_ctx, in_av_stream->codecpar);
	avcodec_open2(dec_ctx, in_decoder, nullptr);

	//SWS context
	SwsContext* sws_ctx = sws_getContext(
		dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
		enc_ctx->width, enc_ctx->height, enc_ctx->pix_fmt,
		SWS_BILINEAR, nullptr, nullptr, nullptr
	);

	//Подготовка AVFrame для энкодера
	AVFrame* enc_frame = av_frame_alloc();
	enc_frame->format = enc_ctx->pix_fmt;
	enc_frame->width = enc_ctx->width;
	enc_frame->height = enc_ctx->height;
	av_frame_get_buffer(enc_frame, 32);

	//Буфер для декодированных фреймов
	AVFrame* dec_frame = av_frame_alloc();
	AVPacket in_pkt;
	av_init_packet(&in_pkt);

	//Главный цикл: читать пакеты, декодировать -> sws_scale -> кодировать -> писать
	while (av_read_frame(i_av_format_ctx, &in_pkt) >= 0) {
		if (in_pkt.stream_index != stream_index) { av_packet_unref(&in_pkt); continue; }
		ret = avcodec_send_packet(dec_ctx, &in_pkt);
		if (ret < 0) { av_packet_unref(&in_pkt); continue; }
		while ((ret = avcodec_receive_frame(dec_ctx, dec_frame)) == 0) {
			//конвертация в YUV420P для х264
			sws_scale(
				sws_ctx,
				dec_frame->data, dec_frame->linesize, 0, dec_ctx->height,
				enc_frame->data, enc_frame->linesize
			);

			//Использовать монотонный счётчик или вычислить на основе времени
			static int64_t pts = 0;
			enc_frame->pts = pts++;

			//Отправляем во энкодер
			ret = avcodec_send_frame(enc_ctx, enc_frame);
			while ((ret = avcodec_receive_packet(enc_ctx, &in_pkt)) == 0) {
				in_pkt.stream_index = out_av_stream->index;
				//скорректировать таймбазу пакета
				av_packet_rescale_ts(&in_pkt, enc_ctx->time_base, out_av_stream->time_base);
				av_interleaved_write_frame(o_av_format_ctx, &in_pkt);
				av_packet_unref(&in_pkt);
			}
		}
		av_packet_unref(&in_pkt);
	}

	//Отправка
	avcodec_send_frame(enc_ctx, nullptr);
	AVPacket out_pkt;
	av_init_packet(&out_pkt);
	while (avcodec_receive_packet(enc_ctx, &out_pkt) == 0) {
		out_pkt.stream_index = out_av_stream->index;
		av_packet_rescale_ts(&out_pkt, enc_ctx->time_base, out_av_stream->time_base);
		av_interleaved_write_frame(o_av_format_ctx, &out_pkt);
		av_packet_unref(&out_pkt);
	}

	av_write_trailer(o_av_format_ctx);
	avformat_close_input(&i_av_format_ctx);
	avformat_free_context(o_av_format_ctx);
	
	return 0;
}