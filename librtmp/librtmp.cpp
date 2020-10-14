#include "librtmp.h"
#include "network.h"

#define PCM_SET_CHUNK_SIZE 1
#define PCM_WINDOW_ACKNOWLEDGEMENT_SIZE 5
#define PCM_SET_PEER_BANDWITH 6
#define PCM_USER_CONTROL_MESSAGE 4
#define UCM_STREAM_BEGIN 0
#define RTMP_MSG_COMMAND 20
#define RTMP_MSG_METADATA 18
#define RTMP_MSG_VIDEO 9

void avbuffree(void* opaque, uint8_t* data) {
	delete[] data;
}

int read_packet(void* opaque, uint8_t* buf, int buf_size) {
	RingBuf* s = reinterpret_cast<RingBuf*>(opaque);
	while (!s->DataAvailable(buf_size)) {}
	s->Read(reinterpret_cast<char*>(buf), buf_size);
	return buf_size;
}

LibRTMP::LibRTMP(uint16_t port):
	ser_thread(&LibRTMP::ServerThread, this),
	serverSocket(networkManager.StartServer(port))
{
	NDIlib_initialize();
	printf("Starting server");	
	runing = true;
	ser_thread.detach();	
}

LibRTMP::~LibRTMP()
{
	runing = false;
	serverSocket.Close();
	while (!stoped) {}
	NDIlib_destroy();
}

void LibRTMP::ServerThread()
{
	while (!runing) {}	
	printf("Listening...");	
	while (runing) {
		auto clientSocket = serverSocket.Accept();
		printf("Incoming connection");
		if (!clientSocket.IsValid()) {
			printf("accept failed with error: %d\n", WSAGetLastError());
			break;
		}
		std::thread cli_thread(&LibRTMP::ClientThread, this, clientSocket);
		cli_thread.detach();
	}
	stoped = true;
}

void LibRTMP::ClientThread(ClientSocket clientSocket)
{
	int iResult = 0;
	//rtmp handshake
	//wait for C0
	RTMP::Handshake::C0* c0 = new RTMP::Handshake::C0{ 0 };
	RTMP::Handshake::C1* s1 = new RTMP::Handshake::C1{ 0 };
	clientSocket.Receive(c0,sizeof(*c0));
	printf("RTMP version %d", c0->version);
	clientSocket.Send(c0, sizeof(*c0));
	s1->random[0] = 1;
	s1->random[1] = 2;
	s1->random[2] = 3;
	clientSocket.Send(s1, sizeof(*s1));
	//rec C1
	clientSocket.Receive(s1, sizeof(*s1));
	clientSocket.Send(s1, sizeof(*s1));
	//rec C2
	clientSocket.Receive(s1, sizeof(*s1));
	delete c0;
	delete s1;

	//HANDSHAKE SUCCESS
	SessionData session_data;
	session_data.chunk_size = 128;
	session_data.data_buf = new char[session_data.chunk_size];
	session_data.videoBuffer = 0;
	session_data.chunk_output = new ChunkStreamOutput(2, &session_data, &clientSocket);
	session_data.chunk_output3 = new ChunkStreamOutput(3, &session_data, &clientSocket);
	uint32_t message_length;
	uint32_t timestamp;

	//chunk streams
	std::unordered_map<uint32_t, ChunkStream*> chunk_streams;

	//listen chunk	
	RTMP::Chunk::BasicHeader* header = new RTMP::Chunk::BasicHeader{ 0 };

	while (true) {
		message_length = 0;
		timestamp = 0;
		if (!clientSocket.Receive(header,sizeof(*header))) {
			break;
		}
		if (header->id < 2) { throw 0; }

		if (header->fmt == 0 && header->id > 1) {
			RTMP::Chunk::MessageHeaderT0* mes_header = new RTMP::Chunk::MessageHeaderT0{ 0 };
			clientSocket.Receive(mes_header,sizeof(*mes_header));
			try {
				chunk_streams.at(header->id);
			}
			catch (std::exception& e) {
				chunk_streams.insert(std::make_pair<uint32_t, ChunkStream*>(header->id, new ChunkStream(3145728, header->id, &session_data)));
			}
			memcpy(reinterpret_cast<char*>(&message_length) + 1, mes_header->message_length, 3);
			message_length = ntohl(message_length);
			chunk_streams[header->id]->SetMessageLength(message_length);
			chunk_streams[header->id]->SetMessageStream(mes_header->message_stream);
			chunk_streams[header->id]->SetMessageType(mes_header->message_type);
			memcpy(reinterpret_cast<char*>(&timestamp)+1, mes_header->timestamp, 3);
			timestamp = ntohl(timestamp);
			chunk_streams[header->id]->SetMessageTimestamp(timestamp);
			delete mes_header;
		}
		else if (header->fmt == 1 && header->id > 1) {
			RTMP::Chunk::MessageHeaderT1* mes_header = new RTMP::Chunk::MessageHeaderT1{ 0 };
			clientSocket.Receive(mes_header, sizeof(*mes_header));
			memcpy(reinterpret_cast<char*>(&message_length) + 1, mes_header->message_length, 3);
			message_length = ntohl(message_length);
			chunk_streams[header->id]->SetMessageLength(message_length);
			chunk_streams[header->id]->SetMessageType(mes_header->message_type);
			memcpy(reinterpret_cast<char*>(&timestamp) + 1, mes_header->timestamp_delta, 3);
			timestamp = ntohl(timestamp);
			chunk_streams[header->id]->AddMessageTimestamp(timestamp);
			delete mes_header;
		}
		else if (header->fmt == 2 && header->id > 1) {
			RTMP::Chunk::MessageHeaderT2* mes_header = new RTMP::Chunk::MessageHeaderT2{ 0 };
			clientSocket.Receive(mes_header, sizeof(*mes_header));
			memcpy(reinterpret_cast<char*>(&timestamp) + 1, mes_header->timestamp_delta, 3);
			timestamp = ntohl(timestamp);
			chunk_streams[header->id]->AddMessageTimestamp(timestamp);
			delete mes_header;
		}
		else if (header->fmt == 3) {

		}
		clientSocket.Receive(session_data.data_buf, min(chunk_streams[header->id]->GetReadLast(), session_data.chunk_size));
		chunk_streams[header->id]->Write(session_data.data_buf, min(chunk_streams[header->id]->GetReadLast(), session_data.chunk_size));
	}
	delete session_data.chunk_output;
	delete session_data.chunk_output3;
	for (auto& i : chunk_streams) {
		delete i.second;
	}
}


ChunkStream::ChunkStream(int buflen, uint8_t chunk_stream, SessionData* session_data) :
	buflen(buflen),
	chunk_stream(chunk_stream),
	session_data(session_data),
	codec(avcodec_find_decoder(AV_CODEC_ID_H264))
{
	NDI_video_frame.p_data = 0;
	data_buf = new char[buflen];	
	pkt = av_packet_alloc();
	frame = av_frame_alloc();
	parser = av_parser_init(codec->id);//5
	c = avcodec_alloc_context3(codec);
	c->bit_rate = 2500000;
	c->width = 0;
	c->height = 0;
	c->pix_fmt = AV_PIX_FMT_YUV420P;
	c->has_b_frames = 2;
	c->time_base = AVRational{ 1,50 };
	c->sw_pix_fmt = AV_PIX_FMT_YUV420P;
	c->framerate = AVRational{ 25,1 };
	c->color_primaries = AVCOL_PRI_BT709;
	c->color_trc = AVCOL_TRC_BT709;
	c->colorspace = AVCOL_SPC_BT709;
	c->color_range = AVCOL_RANGE_MPEG;
	c->chroma_sample_location = AVCHROMA_LOC_LEFT;
	c->field_order = AV_FIELD_PROGRESSIVE;
	c->profile = 100;
	c->level = 40;
}

ChunkStream::~ChunkStream()
{
	delete[] data_buf;

	if (NDI_video_frame.p_data)
		free(NDI_video_frame.p_data);
	if (pNDI_send)
		NDIlib_send_destroy(pNDI_send);
}

void ChunkStream::SetMessageLength(uint32_t mes_len)
{
	message_length = mes_len;
}

void ChunkStream::SetMessageStream(uint32_t str)
{
	message_stream = str;
}

void ChunkStream::SetMessageType(uint8_t t)
{
	message_type = t;
}

void ChunkStream::SetMessageTimestamp(uint32_t time)
{
	if (time == 16777215) { throw 0; }
	timestamp = time;
}

void ChunkStream::AddMessageTimestamp(uint32_t time)
{
	timestamp += time;
}

uint32_t ChunkStream::GetReadLast()
{
	return message_length - write_pos;
}

void ChunkStream::Write(char* buf, int len)
{
	memcpy(data_buf + write_pos, buf, len);
	write_pos += len;
	if (write_pos == message_length) {
		if (chunk_stream == 2) {//protocol control message
			if (message_type == 1) {
				memcpy(&session_data->chunk_size, data_buf, 4);
				session_data->chunk_size = ntohl(session_data->chunk_size);
				delete[] session_data->data_buf;
				session_data->data_buf = new char[session_data->chunk_size];
			}
		}
		if (message_type == RTMP_MSG_COMMAND) {//AMF0 command
			RTMP::Message::RtmpCommand amfCmd(data_buf,message_length);
			if (amfCmd.command_name == "connect") {
				char* resp_buf = new char[5];
				uint32_t* window_size = reinterpret_cast<uint32_t*>(resp_buf);
				*window_size = 5000000;
				*window_size = htonl(*window_size);
				RTMP::Message::RtmpMessage* response = new RTMP::Message::RtmpMessage(PCM_WINDOW_ACKNOWLEDGEMENT_SIZE, 4,0,0,resp_buf);
				session_data->chunk_output->SendRTMPMessage(response);

				resp_buf = new char[5];
				window_size = reinterpret_cast<uint32_t*>(resp_buf);
				*window_size = 5000000;
				*window_size = htonl(*window_size);
				resp_buf[4] = 2;
				response = new RTMP::Message::RtmpMessage(PCM_SET_PEER_BANDWITH, 5, 0, 0, resp_buf);
				session_data->chunk_output->SendRTMPMessage(response);

				resp_buf = new char[4];
				window_size = reinterpret_cast<uint32_t*>(resp_buf);
				*window_size = 4096;
				*window_size = htonl(*window_size);
				response = new RTMP::Message::RtmpMessage(PCM_SET_CHUNK_SIZE, 4, 0, 0, resp_buf);
				session_data->chunk_output->SendRTMPMessage(response);

				RTMP::Message::RtmpCommand resp_result;
				resp_result.command_name = "_result";
				resp_result.transaction_id = 1;
				auto pair = AMFKeyValue{ "fmsVer",AMFValue(AMFType::STRING) };
				pair.value.data_string = "FMS/3,0,1,123";
				resp_result.parameters.push_back(pair);

				pair.key = "capabilities";
				pair.value = AMFValue(AMFType::NUMBER);
				pair.value.data_number = 31;
				resp_result.parameters.push_back(pair);

				pair.key = "level";
				pair.value = AMFValue(AMFType::STRING);
				pair.value.data_string="status";
				resp_result.parameters_extra.push_back(pair);

				pair.key = "code";
				pair.value = AMFValue(AMFType::STRING);
				pair.value.data_string = "NetConnection.Connect.Success";
				resp_result.parameters_extra.push_back(pair);

				pair.key = "description";
				pair.value = AMFValue(AMFType::STRING);
				pair.value.data_string = "Connection succeeded.";
				resp_result.parameters_extra.push_back(pair);

				pair.key = "objectEncoding";
				pair.value = AMFValue(AMFType::NUMBER);
				pair.value.data_number = 0;
				resp_result.parameters_extra.push_back(pair);

				int buflen = 0;
				resp_buf = resp_result.Serialize(&buflen);
				response = new RTMP::Message::RtmpMessage(RTMP_MSG_COMMAND, buflen, 0, 0, resp_buf);
				session_data->chunk_output3->SendRTMPMessage(response);
			}
			if (amfCmd.command_name == "createStream") {
				RTMP::Message::RtmpCommand resp_result;
				resp_result.command_name = "_result";
				resp_result.transaction_id = amfCmd.transaction_id;
				AMFValue createStreamId(AMFType::NUMBER);
				createStreamId.data_number = 1;
				resp_result.values_extra.push_back(createStreamId);

				int buflen = 0;
				char* resp_buf = resp_result.Serialize(&buflen);
				RTMP::Message::RtmpMessage* response = new RTMP::Message::RtmpMessage(RTMP_MSG_COMMAND, buflen, 0, 0, resp_buf);
				session_data->chunk_output3->SendRTMPMessage(response);
			}
			if (amfCmd.command_name == "publish") {
				RTMP::Message::RtmpCommand resp_result;
				resp_result.command_name = "onStatus";
				resp_result.transaction_id = 0;
				auto pair = AMFKeyValue{ "level",AMFValue(AMFType::STRING) };
				pair.value.data_string = "status";
				resp_result.parameters_extra.push_back(pair);

				pair.key = "code";
				pair.value = AMFValue(AMFType::STRING);
				pair.value.data_string = "NetStream.Publish.Start";
				resp_result.parameters_extra.push_back(pair);

				pair.key = "description";
				pair.value = AMFValue(AMFType::STRING);
				pair.value.data_string = "Start publishing";
				resp_result.parameters_extra.push_back(pair);

				int buflen = 0;
				char* resp_buf = resp_result.Serialize(&buflen);
				RTMP::Message::RtmpMessage *response = new RTMP::Message::RtmpMessage(RTMP_MSG_COMMAND, buflen, 0, message_stream, resp_buf);
				session_data->chunk_output3->SendRTMPMessage(response);
			}
			
		}
		if (message_type == RTMP_MSG_METADATA) {
			RTMP::Message::RtmpMetadata amdMeta(data_buf, message_length);
			bool containsMeta = false;
			for (auto& i : amdMeta.parameters) {
				if (i.GetType() == AMFType::STRING) {
					if (i.data_string == "onMetaData") {
						containsMeta = true;
					}
				}
				if (i.GetType() == AMFType::ECMA_ARRAY&&containsMeta) {
					for (auto& i2 : i.data_array) {
						if (i2.key == "width") {
							session_data->width = i2.value.data_number;							
						}
						else if (i2.key == "height") {
							session_data->height = i2.value.data_number;
						}
					}
				}
			}			
		}
		if (message_type == RTMP_MSG_VIDEO) {		
#pragma pack(push,1)
			struct VH{
				uint8_t codecId : 4;
				uint8_t frameType : 4;
				uint8_t avcPacketType;
			} *video_header = reinterpret_cast<VH*>(data_buf);
#pragma pack(pop)
			if (video_header->avcPacketType == 0) {
				c->extradata = reinterpret_cast<uint8_t*>(av_malloc(AV_INPUT_BUFFER_PADDING_SIZE + message_length));
				c->extradata_size = message_length-5;
				memcpy(c->extradata, data_buf + 5, message_length - 5);
				c->width = session_data->width;
				c->height = session_data->height;
				auto ret = avcodec_open2(c, codec, NULL);

				//init sender
				pNDI_send = NDIlib_send_create(nullptr);				
				NDI_video_frame.xres = c->width;
				NDI_video_frame.yres = c->height;
				NDI_video_frame.FourCC = NDIlib_FourCC_type_YV12;
				NDI_video_frame.p_data = (uint8_t*)malloc(NDI_video_frame.xres * NDI_video_frame.yres * 1.5f);
			}
			else {
				//session_data->videoBuffer->Write(data_buf+5, message_length-5);				
				auto ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size,
					reinterpret_cast<uint8_t*>(data_buf+5), message_length-5, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
				ret = avcodec_send_packet(c, pkt);
				ret = avcodec_receive_frame(c, frame);
				if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {					
					memcpy(NDI_video_frame.p_data,frame->data[0], NDI_video_frame.xres* NDI_video_frame.yres);
					memcpy(NDI_video_frame.p_data+ 2073600, frame->data[2], NDI_video_frame.xres* NDI_video_frame.yres/4);
					memcpy(NDI_video_frame.p_data + 2073600+ 518400, frame->data[1], NDI_video_frame.xres* NDI_video_frame.yres/4);
					NDIlib_send_send_video_v2(pNDI_send, &NDI_video_frame);

				}
			}
		}

		write_pos = 0;
	}
}

AMFValue::AMFValue()
{
}

AMFValue::AMFValue(char* data)
{
	switch (data[0]) {
	case 0://number
		type = AMFType::NUMBER;
		break;
	case 1:
		type = AMFType::BOOLEAN;
		break;
	case 2:
		type = AMFType::STRING;
		break;
	case 5:
		type = AMFType::NUL;
		break;
	case 8:
		type = AMFType::ECMA_ARRAY;
		break;
	case 10:
		type = AMFType::STRICT_ARRAY;
		break;
	}

	uint16_t* str_len = 0;
	char str_char[50];
	unsigned __int64* to_swap = 0;
	uint32_t* array_len = 0;
	int read_pos = 0;
	switch (type)
	{
	case AMFType::STRING:
		str_len = reinterpret_cast<uint16_t*>(data+1);
		*str_len = ntohs(*str_len);		
		memset(str_char, 0, 50);
		memcpy(str_char, data + 3, *str_len);
		data_string = str_char;
		break;
	case AMFType::NUMBER:
		memcpy(&data_number, data + 1, 8);		
		to_swap = reinterpret_cast<unsigned __int64*>(&data_number);
		*to_swap = _WS2_32_WINSOCK_SWAP_LONGLONG(*to_swap);
		break;
	case AMFType::BOOLEAN:
		if (data[1]) {
			data_boolean = true;
		}
		break;
	case AMFType::ECMA_ARRAY:
		read_pos = 5;
		while (true) {
			AMFValue key(AMFType::STRING, data + read_pos);
			if (key.data_string.length() == 0)break;
			read_pos += key.GetLength();
			read_pos--;
			AMFValue value(data + read_pos);
			data_array.push_back(AMFKeyValue{ key.data_string,value });
			read_pos += value.GetLength();
		}
		break;
	case AMFType::STRICT_ARRAY:
		read_pos = 1;
		array_len = reinterpret_cast<uint32_t*>(data + read_pos);
		*array_len = ntohl(*array_len);
		read_pos += 4;
		for (int a = 0; a < *array_len; a++) {
			AMFValue value(data + read_pos);
			data_strict_array.push_back(value);
			read_pos += value.GetLength();
		}
		break;
	case AMFType::NUL:
		break;
	default:
		break;
	}
}
AMFValue::AMFValue(AMFType t, char* data):
	type(t)
{
	uint16_t* str_len;
	char str_char[50];
	switch (type)
	{
	case AMFType::STRING:
		str_len = reinterpret_cast<uint16_t*>(data);
		*str_len = ntohs(*str_len);
		memset(str_char, 0, 50);
		memcpy(str_char, data + 2, *str_len);
		data_string = str_char;
		break;
	case AMFType::NUMBER:
		memcpy(&data_number, data, 8);
		data_number = ntohll(data_number);
		break;
	case AMFType::BOOLEAN:
		if (data[0]) {
			data_boolean = true;
		}
		break;
	default:
		break;
	}
}
AMFValue::AMFValue(AMFType t):
	type(t)
{
}
int AMFValue::GetLength()
{
	int res = 0;
	switch (type)
	{
	case AMFType::STRING:
		return data_string.length() + 3;
		break;
	case AMFType::NUMBER:
		return 9;
		break;
	case AMFType::BOOLEAN:
		return 2;
		break;
	case AMFType::ECMA_ARRAY:
		res=8;
		for (auto& i : data_array) {
			res += i.value.GetLength() + i.key.length() + 2;
		}
		return res;
		break;
	case AMFType::STRICT_ARRAY:
		res = 5;
		for (auto& i : data_strict_array) {
			res += i.GetLength();
		}
		return res;
		break;
	case AMFType::NUL:
		return 1;
	default:
		break;
	}
}

char* AMFValue::Serialize()
{
	char* buf = new char[GetLength()];
	uint16_t* str_len_amf;
	uint64_t* to_swap;
	double* amf_double;
	switch (type)
	{
	case AMFType::STRING:
		buf[0] = 2;
		str_len_amf = reinterpret_cast<uint16_t*>(buf+1);
		*str_len_amf = data_string.length();
		*str_len_amf = htons(*str_len_amf);
		memcpy(buf+3,data_string.c_str(),data_string.length());
		break;
	case AMFType::NUMBER:
		buf[0] = 0;
		amf_double = reinterpret_cast<double*>(buf+1);
		*amf_double = data_number;
		to_swap = reinterpret_cast<uint64_t*>(amf_double);
		*to_swap = _WS2_32_WINSOCK_SWAP_LONGLONG(*to_swap);
		break;
	case AMFType::BOOLEAN:
		buf[0] = 1;
		if (data_boolean) {
			buf[1] = 1;
		}
		else {
			buf[1] = 0;
		}
		break;
	default:
		break;
	}
	return buf;
}
AMFType AMFValue::GetType() {
	return type;
}

RTMP::Message::RtmpCommand::RtmpCommand(char* data, int message_length)
{
	int read_pos = 0;
	AMFValue cmd_name_amf(data);
	command_name = cmd_name_amf.data_string;
	read_pos += cmd_name_amf.GetLength();
	if (data[read_pos] != 8) {
		AMFValue trans_id_amf(data + read_pos);
		transaction_id = trans_id_amf.data_number;
		read_pos += trans_id_amf.GetLength();
	}	
	if (data[read_pos] != 5) {
		read_pos++;
		while (true) {
			AMFValue key(AMFType::STRING, data + read_pos);
			if (key.data_string.length() == 0)break;
			read_pos += key.GetLength();
			read_pos--;
			AMFValue value(data + read_pos);
			parameters.push_back(AMFKeyValue{ key.data_string,value });
			read_pos += value.GetLength();
		}
		read_pos += 3;
	}
	else {
		read_pos++;
	}
	while (read_pos != message_length) {
		AMFValue ex(data + read_pos);
		values_extra.push_back(ex);
		read_pos += ex.GetLength();
	}

}

RTMP::Message::RtmpCommand::RtmpCommand()
{
}

char* RTMP::Message::RtmpCommand::Serialize(int* buflen_ret)
{	
	int buflen = 3 + command_name.length() + 9 + 1;
	if (parameters.size() != 0) {
		buflen += 3;
	}
	for (auto& i : parameters) {
		buflen += 2 + i.key.length() + i.value.GetLength();
	}
	for (auto& i : parameters_extra) {
		buflen += 2 + i.key.length() + i.value.GetLength();
	}
	for (auto& i : values_extra) {
		buflen += i.GetLength();
	}
	if (parameters_extra.size() != 0) {
		buflen += 4;
	}
	char* buf = new char[buflen];
	*buflen_ret = buflen;

	int write_pos = 1;
	buf[0] = 2;
	uint16_t* com_n_len = reinterpret_cast<uint16_t*>(buf+write_pos);
	*com_n_len = command_name.length();
	*com_n_len = htons(*com_n_len);
	write_pos += 2;
	memcpy(buf+write_pos,command_name.c_str(),command_name.length());
	write_pos += command_name.length();

	buf[write_pos] = 0;
	write_pos++;
	double* trans_id_amf = reinterpret_cast<double*>(buf + write_pos);
	*trans_id_amf = transaction_id;
	uint64_t* to_swap = reinterpret_cast<uint64_t*>(trans_id_amf);
	*to_swap = _WS2_32_WINSOCK_SWAP_LONGLONG(*to_swap);
	write_pos += 8;

	if (parameters.size() != 0) {
		buf[write_pos] = 3;
		write_pos++;
		for (auto& i : parameters) {
			uint16_t* com_n_len = reinterpret_cast<uint16_t*>(buf + write_pos);
			*com_n_len = i.key.length();
			*com_n_len = htons(*com_n_len);
			write_pos += 2;
			memcpy(buf + write_pos, i.key.c_str(), i.key.length());
			write_pos += i.key.length();

			char* val_buf = i.value.Serialize();
			memcpy(buf + write_pos, val_buf, i.value.GetLength());
			write_pos += i.value.GetLength();
			delete[] val_buf;
		}
		//end
		buf[write_pos] = 0;
		write_pos++;
		buf[write_pos] = 0;
		write_pos++;
		buf[write_pos] = 9;
		write_pos++;
	}
	else {
		buf[write_pos] = 5;
		write_pos++;
	}

	if (parameters_extra.size() != 0) {
		buf[write_pos] = 3;
		write_pos++;
		for (auto& i : parameters_extra) {
			uint16_t* com_n_len = reinterpret_cast<uint16_t*>(buf + write_pos);
			*com_n_len = i.key.length();
			*com_n_len = htons(*com_n_len);
			write_pos += 2;
			memcpy(buf + write_pos, i.key.c_str(), i.key.length());
			write_pos += i.key.length();
			char* val_buf = i.value.Serialize();
			memcpy(buf + write_pos, val_buf, i.value.GetLength());
			write_pos += i.value.GetLength();
			delete[] val_buf;
		}
		//end
		buf[write_pos] = 0;
		write_pos++;
		buf[write_pos] = 0;
		write_pos++;
		buf[write_pos] = 9;
		write_pos++;
	}
	for (auto& i : values_extra) {
		char* val_buf = i.Serialize();
		memcpy(buf + write_pos, val_buf, i.GetLength());
		write_pos += i.GetLength();
		delete[] val_buf;
	}
	return buf;
}

RTMP::Message::RtmpMessage::RtmpMessage(uint8_t type, uint32_t payload_length, uint32_t timestamp, uint32_t stream, char* data):
	type(type),
	payload_length(payload_length),
	timestamp(timestamp),
	stream(stream),
	data(data)
{
}

RTMP::Message::RtmpMessage::~RtmpMessage()
{
	delete[] data;
}

ChunkStreamOutput::ChunkStreamOutput(uint8_t chunk_stream, SessionData* session_data, ClientSocket* clientSocket):
	chunk_stream(chunk_stream),
	session_data(session_data),
	clientSocket(clientSocket)
{
}

void ChunkStreamOutput::SendRTMPMessage(RTMP::Message::RtmpMessage* msg)
{
	RTMP::Chunk::BasicHeader header = RTMP::Chunk::BasicHeader{ 0 };
	header.id = chunk_stream;
	header.fmt = 0;
	RTMP::Chunk::MessageHeaderT0 mes_header = RTMP::Chunk::MessageHeaderT0{ 0 };
	uint32_t msg_len = htonl(msg->payload_length);
	memcpy(mes_header.message_length, reinterpret_cast<char*>(&msg_len) + 1, 3);
	mes_header.message_stream = msg->stream;
	mes_header.message_type = msg->type;
	uint32_t timestamp = htonl(msg->timestamp);
	memcpy(mes_header.timestamp, reinterpret_cast<char*>(&timestamp) + 1, 3);

	char* chunk_buf = new char[sizeof(header) + sizeof(mes_header) + msg->payload_length];
	memcpy(chunk_buf, &header, sizeof(header));
	memcpy(chunk_buf+ sizeof(header), &mes_header, sizeof(mes_header));
	memcpy(chunk_buf+ sizeof(header)+sizeof(mes_header), msg->data, msg->payload_length);

	clientSocket->Send(chunk_buf, (sizeof(header) + sizeof(mes_header) + msg->payload_length));
	delete[] chunk_buf;
 	delete msg;
}

RTMP::Message::RtmpMetadata::RtmpMetadata(char* data, int message_length)
{
	int read_pos = 0;	
	while (read_pos != message_length) {
		AMFValue meta_data(data+read_pos);
		parameters.push_back(meta_data);
		read_pos += meta_data.GetLength();
	}
}
