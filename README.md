# RTMP2NDI
This is RTMP server, that can transform stream to NDI, so you can input any RTMP streaming device to your broadcast with minimum latency.
## Limitations
At this moment, only video with H264, YUV 4:2:0 supported.
## Installation
There is only Windows build available. You can download it in [releases](https://github.com/AlexanderBabansky/RTMP2NDI/releases).
## Usage example
Start CLI program. Setup your RTMP Streamer to server. As soon as you start RTMP streaming, NDI stream appears.

Parameters:
* *--help*  to get info
* *-p 1935* to set server port
## Build from source
Source code contains Windows-specific functions, to build on another OS, porting needed.
### Dependencies 
* [FFMpeg (avcodec, avutil, swscale)](https://ffmpeg.org)
* [NDI SDK by NewTek](https://www.ndi.tv/)
### CMake Configuration
* FFMPEG_PATH to directory, where *{FFMPEG_PATH}/bin* contains all *.lib*, and *{FFMPEG_PATH}/include* contains all includes.
* NDI_SDK to NDI root directory
