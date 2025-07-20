#include <stdio.h>
#include <string>
#include <chrono>
#include <thread>
#include "rtc/rtc.hpp"
#include <nlohmann/json.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/avutil.h>
}

using rtc::binary;
//using json = nlohmann::json;

static std::shared_ptr<rtc::DataChannel> initRtc(rtc::PeerConnection pc) {
	rtc::InitLogger(rtc::LogLevel::Info);

	auto dc = pc.createDataChannel("video");

	pc.onLocalDescription([](rtc::Description sdp) {
		
		std::cout << "sdp offer: " << std::string(sdp) << std::endl;
		});
	pc.onLocalCandidate([](rtc::Candidate candidate) {
		std::cout << "ice candidate: " << std::string(candidate) << std::endl;
		});

	pc.setLocalDescription();

	printf("Paste client cdp answer\n");
	std::string sdp, line;
	while (getline(std::cin, line) && !line.empty()) {
		sdp += line + "\n";
	}
	pc.setRemoteDescription({ sdp, "answer" });

	printf("Paste client ice candidate\n");
	while (getline(std::cin, line) && !line.empty()) {
		pc.addRemoteCandidate(rtc::Candidate(line));
	}

	return dc;
}

static AVFormatContext* initCtx(const char* filename) {
	avformat_network_init();

	AVFormatContext* av_format_ctx = avformat_alloc_context();
	if (avformat_open_input(&av_format_ctx, filename, NULL, NULL) != 0) {
		printf("Could not open video\n");
		return nullptr;
	}
	if (avformat_find_stream_info(av_format_ctx, NULL) < 0) {
		printf("Couldn't find info of stream\n");
		return nullptr;
	}
	return av_format_ctx;
}



int main(int argc, char* argv[]) {

	rtc::InitLogger(rtc::LogLevel::Info);

	auto pc = std::make_shared<rtc::PeerConnection>();

	auto dc = pc->createDataChannel("video");

	pc->onLocalDescription([](rtc::Description sdp) {
		std::cout << "sdp offer: " << std::string(sdp) << std::endl;
		});
	pc->onLocalCandidate([](rtc::Candidate candidate) {
		auto json = candidate.toJSON()
		std::cout << "ice candidate: " << candidate << std::endl;
		});

	pc->setLocalDescription();

	printf("Paste client cdp answer\n");
	std::string sdp, line;
	while (getline(std::cin, line) && !line.empty()) {
		sdp += line + "\n";
	}
	pc->setRemoteDescription({ sdp, "answer" });

	printf("Paste client ice candidate\n");
	while (getline(std::cin, line) && !line.empty()) {
		pc->addRemoteCandidate(rtc::Candidate(line));
	}

	dc->onOpen([dc]() {
		printf("DataChannel open, start stream\n");

		AVFormatContext* av_format_ctx = initCtx("C:/Users/geenb/Desktop/Rema.mp4");
		if (!av_format_ctx) {
			printf("Could not init AVFormatContext\n");
			return;
		}

		int stream_index = -1;
		stream_index = av_find_best_stream(av_format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (stream_index == -1) {
			printf("Could not found video stream\n");
			return;
		}

		AVPacket* av_packet = av_packet_alloc();
		while (av_read_frame(av_format_ctx, av_packet) >= 0) {
			if (av_packet->stream_index != stream_index) {
				av_packet_unref(av_packet);
				continue;
			}

			rtc::binary frame(av_packet->size);
			std::memcpy(frame.data(), av_packet->data, av_packet->size);
			dc->send(frame);

			av_packet_unref(av_packet);

			std::this_thread::sleep_for(std::chrono::milliseconds(33));
		}
		av_packet_free(&av_packet);
		avformat_close_input(&av_format_ctx);
	});

	dc->onClosed([]() {
		std::cout << "DataChannel closed." << std::endl;
		});
	
	return 0;
}