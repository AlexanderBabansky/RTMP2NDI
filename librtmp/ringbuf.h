#pragma once
#define LIBDEVIL_API __declspec(dllexport)
#include <mutex>

class RingBuf {
private:
	char* data;
	unsigned int buflen;
	unsigned int readPos = 0, writePos = 0;
	uint64_t readLevel = 0, writeLevel = 0;
	std::mutex mut;
public:
	LIBDEVIL_API RingBuf(unsigned int buflen);
	LIBDEVIL_API ~RingBuf();

	LIBDEVIL_API void Write(char* data, unsigned int data_len);
	LIBDEVIL_API bool DataAvailable(unsigned int data_len);
	LIBDEVIL_API void Read(char* data, unsigned int data_len);
};