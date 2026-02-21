#ifndef UDPSERVER_H
#define UDPSERVER_H

#include <iostream>
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
#define ZeroMemory(dest, len) memset(dest, 0, len)
#endif

using namespace std; //no std::

//globals for server
#ifdef _WIN32
struct udpServer {
    SOCKET in;
    sockaddr_in serverHint;
    uint32_t buffersize;
    sockaddr_in client;
    int clientLength;
    const char* ip;
    uint16_t port;
    char buf[1500];
    char clientIP[256];
    int bytesReceived;
};
#else 
struct udpServer {
    SOCKET in;
    sockaddr_in serverHint;
    uint32_t buffersize;
    sockaddr_in client;
    socklen_t clientLength;
    const char* ip;
    uint16_t port;
    char buf[1500];
    char clientIP[256];
    int bytesReceived;
};
#endif

udpServer udpServerStart(const char* ip, uint16_t port, uint32_t buffersize);
void udpServerCleanUp(udpServer& input);
bool udpServerGet(udpServer& input);

#endif