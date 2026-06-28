cmake_minimum_required(VERSION 3.14)

project(stream-app LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(lib/glfw)

add_compile_definitions(GL_SILENCE_DEPRECATION)

set(FFMPEG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/lib/ffmpeg")
set(FFMPEG_LIBS
	avcodec
	avformat
	avutil
	avfilter
	avdevice
	swscale
	swresample
)

if(APPLE)
	list(APPEND PLATFORM_LIBS
		"-framework OpenGL"
	)
elseif(WIN32)
	list(APPEND PLATFORM_LIBS
		opengl32
		glu32
	)
else()
	list(APPEND PLATFORM_LIBS
		GL
		GLU
		X11
	)
endif()

set(SOURCES_SERVER
	src/server.cpp
)

set(SOURCES_CLIENT
	src/client.cpp
	src/video_reader.hpp
	src/video_reader.cpp
)

add_executable(server ${SOURCES_SERVER})
add_executable(client ${SOURCES_CLIENT})

foreach(target_name IN ITEMS server client)
	target_include_directories(${target_name} PRIVATE
		"${FFMPEG_ROOT}/include"
	)
	target_link_directories(${target_name} PRIVATE
		"${FFMPEG_ROOT}/lib"
	)
	target_link_libraries(${target_name} PRIVATE
		glfw
		${PLATFORM_LIBS}
		${FFMPEG_LIBS}
	)
endforeach()
