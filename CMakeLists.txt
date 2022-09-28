cmake_minimum_required(VERSION 3.13)

project (rtmp2ndi VERSION 0.1)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
option(USE_OPENSSL "Enable OpenSSL" OFF)
option(USE_VCPKG "Use VCPKG" ON)
set(NDI_SDK_PATH CACHE PATH "NDI SDK Path")

find_package(EasyRTMP REQUIRED)

if(WIN32)
	set(NDI_LIB x64/Processing.NDI.Lib.x64.lib)
	set(NDI_BIN x64/Processing.NDI.Lib.x64.dll)
else()
	set(NDI_LIB x86_64-linux-gnu/libndi.so)
	set(NDI_BIN x86_64-linux-gnu/libndi.so)
endif()

if(NOT EXISTS "${NDI_SDK_PATH}/include/Processing.NDI.Lib.h" OR
	NOT EXISTS "${NDI_SDK_PATH}/lib/${NDI_LIB}")	
	message(FATAL_ERROR "NDI SDK not found")
endif()

if (${USE_OPENSSL})	
	add_definitions(-DUSE_OPENSSL)
endif()

if (${USE_VCPKG})
	find_package(FFMPEG REQUIRED)
else()
	

	if (WIN32)
		set(FFMPEG_PATH CACHE PATH "FFMpeg path")
		set(FFMPEG_LIBRARY_DIR ${FFMPEG_PATH}/bin)
		set(FFMPEG_INCLUDE_DIRS ${FFMPEG_PATH}/include)
	else()
		set(FFMPEG_LIBRARY_DIR "/usr/local/lib")
	endif()

	find_library(AVCODEC_LIBRARY avcodec PATHS ${FFMPEG_LIBRARY_DIR})
	find_library(AVFORMAT_LIBRARY avformat PATHS ${FFMPEG_LIBRARY_DIR})
	find_library(AVUTIL_LIBRARY avutil PATHS ${FFMPEG_LIBRARY_DIR})
	find_library(AVSWSCALE_LIBRARY swscale PATHS ${FFMPEG_LIBRARY_DIR})
	find_library(AVSWRESAMPLE_LIBRARY swresample PATHS ${FFMPEG_LIBRARY_DIR})			

	set(FFMPEG_LIBRARIES ${AVCODEC_LIBRARY}; ${AVFORMAT_LIBRARY}; ${AVUTIL_LIBRARY}; ${AVSWSCALE_LIBRARY};${AVSWRESAMPLE_LIBRARY})	

	if (${AVCODEC_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVFORMAT_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVUTIL_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVSWSCALE_LIBRARY} MATCHES "-NOTFOUND$" OR
	${AVSWRESAMPLE_LIBRARY} MATCHES "-NOTFOUND$")
		message(FATAL_ERROR "FFMpeg not found")
	endif()

	file(GLOB FFMPEG_DLLS "${FFMPEG_PATH}/bin/*.dll")
	message(${FFMPEG_DLLS})
endif()

add_executable(rtmp2ndi rtmp2ndi.cpp perfmon.h perfmon.cpp)
target_link_libraries(rtmp2ndi PRIVATE EasyRTMP::easyrtmp ${FFMPEG_LIBRARIES} ${NDI_SDK_PATH}/Lib/${NDI_LIB})
target_include_directories(rtmp2ndi PRIVATE ${NDI_SDK_PATH}/Include ${FFMPEG_INCLUDE_DIRS})
if ($<$<CONFIG:Debug>:true>)
	get_target_property(EASYRTMP_DLL EasyRTMP::easyrtmp IMPORTED_LOCATION_DEBUG)
else()
	get_target_property(EASYRTMP_DLL EasyRTMP::easyrtmp IMPORTED_LOCATION_RELEASE)
endif()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${EASYRTMP_DLL} ${CMAKE_BINARY_DIR}
)
add_custom_command(TARGET rtmp2ndi POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NDI_SDK_PATH}/bin/${NDI_BIN} ${CMAKE_BINARY_DIR}
)

if (NOT ${USE_VCPKG})
	add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FFMPEG_DLLS} ${CMAKE_BINARY_DIR}
	)
endif()

set (RUNTIME_DIRS ".")

if (NOT WIN32)
	set(RUNTIME_DIRS "bin")
endif()

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${RUNTIME_DIRS}
    )
install (FILES
	${NDI_SDK_PATH}/bin/${NDI_BIN}
	${EASYRTMP_DLL}
	${FFMPEG_DLLS}
	DESTINATION ${RUNTIME_DIRS})
