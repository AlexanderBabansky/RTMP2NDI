cmake_minimum_required(VERSION 3.13)

project (rtmp2ndi VERSION 0.1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
option(USE_OPENSSL "Enable OpenSSL" OFF)
option(USE_VCPKG "Use VCPKG" ON)
set(NDI_SDK_PATH CACHE PATH "NDI SDK Path")

find_package(EasyRTMP REQUIRED)

if(NOT EXISTS "${NDI_SDK_PATH}/Include/Processing.NDI.Lib.h" OR
	NOT EXISTS "${NDI_SDK_PATH}/Lib/${NDI_LIB}")	
	message(FATAL_ERROR "NDI SDK not found")
endif()

if(WIN32)
	set(NDI_LIB x64/Processing.NDI.Lib.x64.lib)
	set(NDI_BIN x64/Processing.NDI.Lib.x64.dll)
else()
	set(NDI_LIB x86_64-linux-gnu/libndi.so)
	set(NDI_BIN x86_64-linux-gnu/libndi.so)
endif()

if (${USE_OPENSSL})	
	add_definitions(-DUSE_OPENSSL)
endif()

if (${USE_VCPKG})
	find_package(FFMPEG REQUIRED)
else()
	set(FFMPEG_PATH CACHE PATH "FFMpeg path")

	find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h PATHS ${FFMPEG_PATH}/include)
	find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h PATHS ${FFMPEG_PATH}/include)
	find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h PATHS ${FFMPEG_PATH}/include)
	find_path(AVSWSCALE_INCLUDE_DIR libswscale/swscale.h PATHS ${FFMPEG_PATH}/include)
	find_path(AVSWRESAMPLE_INCLUDE_DIR libswresample/swresample.h PATHS ${FFMPEG_PATH}/include)	

	find_library(AVCODEC_LIBRARY avcodec PATHS ${FFMPEG_PATH}/bin)
	find_library(AVFORMAT_LIBRARY avformat PATHS ${FFMPEG_PATH}/bin)
	find_library(AVUTIL_LIBRARY avutil PATHS ${FFMPEG_PATH}/bin)
	find_library(AVSWSCALE_LIBRARY swscale PATHS ${FFMPEG_PATH}/bin)
	find_library(AVSWRESAMPLE_LIBRARY swresample PATHS ${FFMPEG_PATH}/bin)
	set(FFMPEG_LIBRARIES ${AVCODEC_LIBRARY}; ${AVFORMAT_LIBRARY}; ${AVUTIL_LIBRARY}; ${AVSWSCALE_LIBRARY};${AVSWRESAMPLE_LIBRARY})
	set(FFMPEG_INCLUDE_DIRS ${FFMPEG_PATH}/include)

	if (${AVCODEC_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVFORMAT_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVUTIL_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVSWSCALE_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVSWRESAMPLE_LIBRARY} MATCHES "-NOTFOUND$")
		message(FATAL_ERROR "FFMpeg not found")
	endif()	
endif()

add_executable(rtmp2ndi rtmp2ndi.cpp perfmon.h perfmon.cpp)
target_link_libraries(rtmp2ndi PRIVATE EasyRTMP::easyrtmp ${FFMPEG_LIBRARIES} ${NDI_SDK_PATH}/Lib/${NDI_LIB})
target_include_directories(rtmp2ndi PRIVATE ${NDI_SDK_PATH}/Include ${FFMPEG_INCLUDE_DIRS})
target_link_directories(rtmp2ndi PRIVATE ${FFMPEG_LIBRARY_DIRS})
add_custom_command(TARGET rtmp2ndi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NDI_SDK_PATH}/bin/${NDI_BIN} ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_PATH}/bin/avformat-59.dll ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_PATH}/bin/avcodec-59.dll ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_PATH}/bin/avdevice-59.dll ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_PATH}/bin/avfilter-8.dll ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_PATH}/bin/avutil-57.dll ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_PATH}/bin/libmp3lame.dll ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_PATH}/bin/openh264.dll ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_PATH}/bin/swresample-4.dll ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_PATH}/bin/swscale-6.dll ${CMAKE_BINARY_DIR}
)
