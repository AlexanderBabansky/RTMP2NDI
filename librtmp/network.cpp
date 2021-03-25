#include "network.h"
#include <stdexcept>

using namespace std;

NetworkManager::NetworkManager()
{
	int iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0) {
		printf("WSAStartup failed with error: %d\n", iResult);
		throw runtime_error("WSAStartup failed with error");
	}
}

ServerSocket NetworkManager::StartServer(uint16_t port_i)
{
	char port[6];
	itoa(port_i, port, 10);
	struct addrinfo hints;
	struct addrinfo* result = NULL;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;
	int iResult = getaddrinfo(NULL, port, &hints, &result);
	if (iResult != 0) {
		printf("getaddrinfo failed with error: %d\n", iResult);
		WSACleanup();
		throw runtime_error("getaddrinfo failed with error");
	}
	SOCKET ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET) {
		printf("socket failed with error: %ld\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		throw runtime_error("socket failed with error");
	}
	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		printf("bind failed with error: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		throw runtime_error("bind failed with error");
	}
	freeaddrinfo(result);
	iResult = listen(ListenSocket, SOMAXCONN);
	if (iResult == SOCKET_ERROR) {
		printf("listen failed with error: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		throw runtime_error("listen failed with error");
	}
	return ServerSocket(ListenSocket);
}

NetworkManager::~NetworkManager()
{
	WSACleanup();
}

ServerSocket::ServerSocket(SOCKET ListenSocket) :
	ListenSocket(ListenSocket)
{
}

ClientSocket ServerSocket::Accept()
{
	SOCKET s = accept(ListenSocket, NULL, NULL);
	return ClientSocket(s);
}

void ServerSocket::Close()
{
	closesocket(ListenSocket);
}

ServerSocket::~ServerSocket()
{
	closesocket(ListenSocket);
}

ClientSocket::ClientSocket(SOCKET s) :
	clientSocket(s)
{
}

ClientSocket::~ClientSocket()
{
}
bool ClientSocket::Receive(void* buf, uint32_t buf_len)
{
	uint32_t received = 0;
	while (received != buf_len) {
		auto r = recv(clientSocket, reinterpret_cast<char*>(buf) + received, buf_len - received, 0);
		if (r <= 0) {
			return false;
		}
		received += r;
	}
}

void ClientSocket::Send(void* buf, uint32_t buf_len)
{
	send(clientSocket, reinterpret_cast<char*>(buf), buf_len, 0);
}

bool ClientSocket::IsValid()
{
	return (clientSocket > 0) ? true : false;
}

void ClientSocket::Close()
{
	closesocket(clientSocket);
}


