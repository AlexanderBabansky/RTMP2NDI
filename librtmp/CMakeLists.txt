cmake_minimum_required(VERSION 3.10)

project(librtmp VERSION 0.1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

set(FFMPEG_PATH "E:" CACHE PATH "FFMpeg Path")
set(NDI_SDK "E:" CACHE PATH "NDI SDK Path")
include_directories(
	${FFMPEG_PATH}/include
	${NDI_SDK}/Include)

# add the executable
add_library(librtmp SHARED
	librtmp.cpp
	ringbuf.cpp
	network.cpp
)

target_link_libraries(librtmp
	${FFMPEG_PATH}/bin/avcodec.lib
	${FFMPEG_PATH}/bin/avformat.lib
	${FFMPEG_PATH}/bin/avutil.lib
	${FFMPEG_PATH}/bin/x264.lib
	${NDI_SDK}/Lib/x64/Processing.NDI.Lib.x64.lib
	Ws2_32.lib)
	
install(TARGETS librtmp DESTINATION bin)