cmake_minimum_required(VERSION 3.14)

set(CMAKE_TOOLCHAIN_FILE "C:/Users/geenb/Documents/vcpkg/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")

project(stream-app C CXX)
set(CMAKE_CXX_STANDARD 17)

add_subdirectory(lib/glfw)

add_definitions(-DGL_SILENCE_DEPRESENTATION)

include_directories(lib/ffmpeg/include)
link_directories(lib/ffmpeg/lib)

find_package(nlohmann_json 3.12.0 REQUIRED)

find_package(LibDataChannel CONFIG REQUIRED)

find_package(Boost REQUIRED COMPONENTS Asio)

if(APPLE) 
	list(APPEND EXTRA_LIBS
		"-framework OpenGL"
	)
elseif(WIN32)
	list(APPEND EXTRA_LIBS
		opengl32
		glu32
	)
else()
	list(APPEND EXTRA_LIBS
		GL 
		GLU 
		X11
	)
endif()

list(APPEND SOURSES_SERVER
	src/server.cpp
)

list(APPEND SOURSES_CLIENT
	src/client.cpp
	src/video_reader.hpp
	src/video_reader.cpp
)

add_executable(server ${SOURSES_SERVER})
target_link_libraries(server 
	glfw 
	${EXTRA_LIBS}
	avcodec
	avformat
	avutil
	avfilter
	avdevice
	swscale
	swresample
	Boost::asio

	LibDataChannel::LibDataChannel
	nlohmann_json::nlohmann_json
)

add_executable(client ${SOURSES_CLIENT})
target_link_libraries(client 
	glfw 
	${EXTRA_LIBS}
	avcodec
	avformat
	avutil
	avfilter
	avdevice
	swscale
	swresample
	Boost::asio

	LibDataChannel::LibDataChannel
	nlohmann_json::nlohmann_json
)

