cmake_minimum_required(VERSION 3.10)

project(rtmp-qt VERSION 0.1)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)
#set(CMAKE_AUTOMOC ON)
#set(CMAKE_AUTORCC ON)
#set(CMAKE_AUTOUIC ON)
if(CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif()

#find_package(Qt5 COMPONENTS Widgets REQUIRED)

add_subdirectory(librtmp)

include_directories(
	${FFMPEG_PATH}/include
	${NDI_SDK}/Include)
	
#add_executable(rtmp-qt
#	main.cpp
#	rtmp_qt.ui
#	about.ui
#)

add_executable(rtmp2ndi-cli
	cli.cpp)
	
#target_link_libraries(rtmp-qt
#	librtmp
#	Qt5::Widgets)

target_link_libraries(rtmp2ndi-cli
	librtmp)

#install(TARGETS rtmp-qt DESTINATION bin)
install(TARGETS rtmp2ndi-cli DESTINATION bin)