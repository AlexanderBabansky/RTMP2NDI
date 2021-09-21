# RTMP2NDI
Portable command line RTMP server with NDI output

## Usage
To get help:
```
rtmp2ndi --help
```
By default, it accepts any stream and output with NDI where name is streaming key.
Supports TLS rtmps://. Supports any codec supported by FLV specification, but tested
only with H264 and AAC. Also can support other codecs, if has agreement with
streamer. Works fine up to FullHD, but has problems with UHD 4K, will be fixed
soon. Video resoultion currently is limited by CPU power converting pixel format.
When resolution is to high, artifacts appear.

## Dependencies
* C++11 compiler (tested on MSVCv14.29, gcc 8.3.0)
* [EasyRTMP](https://github.com/AlexanderBabansky/EasyRTMP)
* [FFMpeg](https://ffmpeg.org) (avcodec,avformat,avutil,swscale,swresample)
* [NDI SDK](https://ndi.tv/sdk) (we detected problems with version 5 on Linux builds, 4.5 works fine)
* [OpenSSL](https://www.openssl.org) (optional, to support RTMPS)

## Building
Project has CMake build script

## Acknowledgements
* [cxxopts](https://github.com/jarro2783/cxxopts) by jarro2783
* [FFMpeg](https://ffmpeg.org)
* [NDI](https://ndi.tv) by NewTek
