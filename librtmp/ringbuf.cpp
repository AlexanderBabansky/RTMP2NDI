#include <memory>
#include "ringbuf.h"

RingBuf::RingBuf(unsigned int buflen) :
	buflen(buflen)
{
	data = new char[buflen];
}
RingBuf::~RingBuf()
{
	delete[] data;
}

void RingBuf::Write(char* data, unsigned int data_len)
{
	mut.lock();
	if (data_len/2 >= buflen) {
		throw 0;
	}
	if (writePos + data_len <= buflen) {
		memcpy(this->data + writePos, data, data_len);
		writePos += data_len;
	}
	else {
		memcpy(this->data + writePos, data, buflen - writePos);
		memcpy(this->data, data + buflen - writePos, data_len - buflen + writePos);
		writePos = data_len - buflen + writePos;
		writeLevel++;
	}
	mut.unlock();
}

bool RingBuf::DataAvailable(unsigned int data_len)
{
	mut.lock();
	if ((readLevel == writeLevel && readPos + data_len <= writePos) || (readLevel < writeLevel && (buflen - readPos >= data_len || writePos >= data_len - (buflen - readPos)))) {
		mut.unlock();
		return true;
	}
	mut.unlock();
	return false;
}

void RingBuf::Read(char* data, unsigned int data_len)
{
	mut.lock();
	if (readPos + data_len <= buflen) {
		memcpy(data, this->data + readPos, data_len);
		readPos += data_len;
	}
	else {
		memcpy(data, this->data + readPos, buflen - readPos);
		memcpy(data + buflen - readPos, this->data, data_len - buflen + readPos);
		readPos = data_len - buflen + readPos;
		readLevel++;
		if (readLevel == writeLevel) {
			readLevel = 0; writeLevel = 0;
		}
	}
	mut.unlock();
}
