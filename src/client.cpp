#include <stdio.h>
#include <GLFW/glfw3.h>
#include "video_reader.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <string>

static std::string get_stream_url(int argc, char* argv[]) {
	std::string stream_url = "udp://127.0.0.1:9000?fifo_size=5000000";

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];
		if ((arg == "--url" || arg == "-u") && i + 1 < argc) {
			stream_url = argv[++i];
		}
		else if (arg.rfind("--url=", 0) == 0) {
			stream_url = arg.substr(6);
		}
		else if (arg == "--help" || arg == "-h") {
			printf("Usage: client [--url udp://127.0.0.1:9000?fifo_size=5000000]\n");
			return "";
		}
		else {
			stream_url = arg;
		}
	}

	return stream_url;
}

int main(int argc, char* argv[]) {
	GLFWwindow* window;
	std::string stream_url = get_stream_url(argc, argv);
	if (stream_url.empty()) {
		return 0;
	}

	if (!glfwInit()) {
		printf("Couldn't init GLFW\n");
		return 1;
	}

	VideoReaderState vr_state{};
	if (!video_reader_open(&vr_state, stream_url.c_str())) {
		printf("Couldn't open video file\n");
		glfwTerminate();
		return 1;
	}

	int frame_width = 0;
	int frame_height = 0;
	uint8_t* data = nullptr;
	 
	int window_width = 600;
	int window_height = 600;
	window = glfwCreateWindow(window_width, window_height, "Stream", NULL, NULL);
	if (!window) {
		printf("Couldn't open window\n");
		video_reader_close(&vr_state);
		glfwTerminate();
		return 1;
	}

	glfwMakeContextCurrent(window);

	GLuint tex_handle;
	glGenTextures(1, &tex_handle);
	glBindTexture(GL_TEXTURE_2D, tex_handle);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	while (!glfwWindowShouldClose(window)) {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		glfwGetFramebufferSize(window, &window_width, &window_height);

		glViewport(0, 0, window_width, window_height);

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glOrtho(0, window_width, window_height, 0, -1, 1);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		int64_t pts;
		if (!video_reader_read(&vr_state, data, &pts)) {
			printf("Couldn't load video frame\n");
			glfwPollEvents();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			continue;
		}

		frame_width = vr_state.width;
		frame_height = vr_state.height;

		glBindTexture(GL_TEXTURE_2D, tex_handle);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width, frame_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

		float frame_k = (float)frame_width / frame_height;
		float window_k = (float)window_width / window_height;
		float k;
		int draw_width, draw_height;
		if (frame_k >= window_k) {
			k = (float)window_width / frame_width;
			draw_width = (int)(frame_width * k);
			draw_height = (int)(frame_height * k);
		}
		else {
			k = (float)window_height / frame_height;
			draw_width = (int)(frame_width * k);
			draw_height = (int)(frame_height * k);
		}
		int offset_x = (window_width - draw_width) / 2;
		int offset_y = (window_height - draw_height) / 2;

		// Render texture
		glEnable(GL_TEXTURE_2D);
		glBegin(GL_QUADS);
		glTexCoord2d(0, 0); glVertex2i(offset_x, offset_y);
		glTexCoord2d(1, 0); glVertex2i(offset_x + draw_width, offset_y);
		glTexCoord2d(1, 1); glVertex2i(offset_x + draw_width, offset_y + draw_height);
		glTexCoord2d(0, 1); glVertex2i(offset_x, offset_y + draw_height);
		glEnd();
		glDisable(GL_TEXTURE_2D);


		glfwSwapBuffers(window);
		glfwPollEvents();
		std::this_thread::sleep_for(std::chrono::milliseconds(33));
	}

	glDeleteTextures(1, &tex_handle);
	glfwDestroyWindow(window);
	glfwTerminate();
	video_reader_close(&vr_state);
	delete[] data;

	return 0;
}
