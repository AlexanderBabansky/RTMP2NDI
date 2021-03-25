#pragma once
#define LIBRTMP_API __declspec(dllexport)
#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include "Processing.NDI.Lib.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <stdio.h>
#include <windows.h>
#include <winsock2.h>
#include <stdlib.h>
#include <WS2tcpip.h>
#include <stdint.h>
#include <thread>
#include <string>
#include <unordered_map>
#include "ringbuf.h"
#include "network.h"

struct SessionData;

enum class AMFType {
	STRING = 2,
	NUMBER = 0,
	BOOLEAN = 1,
	NUL = 5,
	ECMA_ARRAY = 8,
	STRICT_ARRAY = 10
};
class AMFValue;
class AMFKeyValue;

class AMFValue {
private:
	AMFType type;
public:
	std::string data_string;
	double data_number = 0;
	bool data_boolean = false;
	std::list<AMFKeyValue> data_array;
	std::vector<AMFValue> data_strict_array;
	AMFValue();
	AMFValue(char* data);
	AMFValue(AMFType t, char* data);
	AMFValue(AMFType t);
	AMFType GetType();
	int GetLength();
	char* Serialize();
};

struct AMFKeyValue {
	std::string key;
	AMFValue value;
};

namespace RTMP {
	namespace Message {
		class RtmpCommand {//non serializable
		private:

		public:
			std::string command_name;
			double transaction_id = 0;
			std::list<AMFKeyValue> parameters;
			std::list<AMFKeyValue> parameters_extra;
			std::list<AMFValue> values_extra;
			RtmpCommand(char* data, int message_length);
			RtmpCommand();
			char* Serialize(int* buflen_ret);
		};
		class RtmpMetadata {
		public:
			std::list<AMFValue> parameters;
			RtmpMetadata(char* data, int message_length);
		};
		class RtmpMessage {
		private:

		public:
			uint8_t type;
			uint32_t payload_length;
			uint32_t timestamp;
			uint32_t stream;
			char* data;

			RtmpMessage(uint8_t type, uint32_t payload_length, uint32_t timestamp, uint32_t stream, char* data);
			~RtmpMessage();
			char* Serialize();
		};
	}
}
#pragma pack(push,1)
namespace RTMP {
	namespace Handshake {
		struct C0 {
			uint8_t version;
		};
		struct C1 {
			uint32_t time;
			uint32_t zero;
			char random[1528];
		};
	}
	namespace Chunk {
		struct BasicHeader {
			uint8_t id : 6;
			uint8_t fmt : 2;
		};
		struct MessageHeaderT0 {
			uint8_t timestamp[3];
			uint8_t message_length[3];
			uint8_t message_type;
			uint32_t message_stream;//le
		};
		struct MessageHeaderT1 {
			uint8_t timestamp_delta[3];
			uint8_t message_length[3];
			uint8_t message_type;
		};
		struct MessageHeaderT2 {
			uint8_t timestamp_delta[3];
		};
		struct ExtendedTimestamp {
			uint32_t timestamp;
		};
	}
	namespace Message {
		struct UserControlMessage {
			uint16_t type;
			uint32_t data;
		};
	}
}
#pragma pack(pop)

class ChunkStream {
private:

	uint8_t chunk_stream = 0;
	uint32_t write_pos = 0, message_length = 0;
	uint32_t message_stream = 0;
	uint8_t message_type = 0;
	uint32_t timestamp = 0;
	SessionData* session_data;

	AVPacket* pkt;
	AVFrame* frame;
	const AVCodec* codec;
	AVCodecParserContext* parser;
	AVCodecContext* c;
	AVFormatContext* fmt_ctx = NULL;
	AVIOContext* avio_ctx;
	struct SwsContext* sws_ctx;
	uint8_t* dst_data[4];
	int dst_linesize[4];
	uint8_t* avio_ctx_buffer = NULL;
	size_t avio_ctx_buffer_size = 4096;
	bool image_loaded = false;

	NDIlib_send_instance_t pNDI_send = NULL;
	NDIlib_video_frame_v2_t NDI_video_frame;
public:
	char* data_buf;
	int buflen;

	ChunkStream(int buflen = 0, uint8_t chunk_stream = 0, SessionData* session_data = 0);
	~ChunkStream();
	void SetMessageLength(uint32_t mes_len);
	void SetMessageStream(uint32_t str);
	void SetMessageType(uint8_t t);
	void SetMessageTimestamp(uint32_t time);
	void AddMessageTimestamp(uint32_t time);
	uint32_t GetReadLast();
	void Write(char* buf, int len);

};

class ChunkStreamOutput {
private:
	uint8_t chunk_stream;
	uint32_t write_pos = 0;
	SessionData* session_data;
	ClientSocket* clientSocket;
public:
	ChunkStreamOutput(uint8_t chunk_stream, SessionData* session_data, ClientSocket* clientSocket);
	void SendRTMPMessage(RTMP::Message::RtmpMessage* msg);
};

struct SessionData {
	uint32_t chunk_size;
	char* data_buf;
	RingBuf* videoBuffer;
	uint16_t width = 0, height = 0;
	ChunkStreamOutput* chunk_output, * chunk_output3;
};


class LibRTMP {
private:
	std::thread ser_thread;
	bool running = false;
	NetworkManager networkManager;
	ServerSocket serverSocket;
public:
	LIBRTMP_API LibRTMP(uint16_t port);
	LIBRTMP_API ~LibRTMP();
	LIBRTMP_API bool IsRunning();

	void ServerThread();
	void ClientThread(ClientSocket clientSocket);
};