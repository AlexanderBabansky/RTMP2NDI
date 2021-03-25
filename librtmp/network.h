#pragma once
#undef UNICODE
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <stdlib.h>
#include <WS2tcpip.h>
#include <stdint.h>
#include <stdio.h>

class ClientSocket {
private:
	SOCKET clientSocket;
public:
	ClientSocket(SOCKET s);
	bool Receive(void* buf, uint32_t buf_len);
	void Send(void* buf, uint32_t buf_len);
	bool IsValid();
	void Close();
	~ClientSocket();
};

class ServerSocket {
private:
	SOCKET ListenSocket;
public:
	ServerSocket(SOCKET ListenSocket);
	ClientSocket Accept();
	void Close();
	~ServerSocket();
};

class NetworkManager {
private:
	WSADATA wsaData;
public:
	NetworkManager();
	ServerSocket StartServer(uint16_t port);
	~NetworkManager();
};