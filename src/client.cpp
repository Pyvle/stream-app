#include <stdio.h>
#include <GLFW/glfw3.h>
#include "video_reader.hpp"
#include <iostream>
#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {
	GLFWwindow* window;

	if (!glfwInit()) {
		printf("Couldn't init GLFW\n");
		return 1;
	}

	VideoReaderState vr_state;
	if (!video_reader_open(&vr_state, "udp://127.0.0.1:9000?fifo_size=5000000")) {
		printf("Couldn't open video file\n");
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
			continue;
		}

		if (frame_width == 0 || frame_height == 0) {
			frame_width = vr_state.width;
			frame_height = vr_state.height;
		}

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

		//рендер текстуры
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

	delete[] data;
	video_reader_close(&vr_state);

	return 0;
}