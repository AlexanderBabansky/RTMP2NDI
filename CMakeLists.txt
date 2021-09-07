cmake_minimum_required(VERSION 3.13)

project (RTMP2NDI VERSION 0.1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
option(USE_OPENSSL "Enable OpenSSL" OFF)
set(NDI_SDK_PATH CACHE PATH "NDI SDK Path")

if (${USE_OPENSSL})	
	add_definitions(-DUSE_OPENSSL)
endif()

find_package(EasyRTMP REQUIRED)

find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
find_path(AVSWSCALE_INCLUDE_DIR libswscale/swscale.h)
find_path(AVSWRESAMPLE_INCLUDE_DIR libswresample/swresample.h)

find_library(AVCODEC_LIBRARY avcodec)
find_library(AVFORMAT_LIBRARY avformat)
find_library(AVUTIL_LIBRARY avutil)
find_library(AVSWSCALE_LIBRARY swscale)
find_library(AVSWRESAMPLE_LIBRARY swresample)

if (${AVCODEC_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVFORMAT_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVUTIL_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVSWSCALE_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVSWRESAMPLE_LIBRARY} MATCHES "-NOTFOUND$")
	message(FATAL_ERROR "FFMpeg not found")
endif()

if(WIN32)
	set(NDI_LIB x64/Processing.NDI.Lib.x64.lib)
else()
	set(NDI_LIB x86_64-linux-gnu/libndi.so)
endif()

if(NOT EXISTS "${NDI_SDK_PATH}/Include/Processing.NDI.Lib.h" OR
	NOT EXISTS "${NDI_SDK_PATH}/Lib/${NDI_LIB}")	
	message(FATAL_ERROR "NDI SDK not found")
endif()

add_executable(rtmp2ndi rtmp2ndi.cpp)
target_link_libraries(rtmp2ndi PRIVATE EasyRTMP::easyrtmp
	${FFMPEG_LIBRARIES} ${NDI_SDK_PATH}/Lib/${NDI_LIB}
	${AVCODEC_LIBRARY} ${AVFORMAT_LIBRARY} ${AVUTIL_LIBRARY} ${AVSWSCALE_LIBRARY}
	${AVSWRESAMPLE_LIBRARY})
target_include_directories(rtmp2ndi PRIVATE ${NDI_SDK_PATH}/Include ${AVCODEC_INCLUDE_DIR})