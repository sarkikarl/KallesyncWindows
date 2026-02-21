#pragma once

#include<iostream>
#include <string>
#include <cstring>

#ifdef _WIN32
#include <ws2tcpip.h>
#pragma comment (lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define SOCKET int
#define INVALID_SOCKET -1
#define SOCKET_ERROR -1
#define closesocket close
#define WSAGetLastError() errno
#endif

struct udpClient {
    SOCKET out;
    sockaddr_in server;
};

udpClient udpClientStart(const char* ip, int port);

void udpSend(const char* data, size_t len, udpClient tmp);

void udpClientCleanup(udpClient tmp);