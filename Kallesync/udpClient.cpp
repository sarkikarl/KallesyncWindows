#include "udpClient.h"
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
using namespace std; //no std::

//globals for server
#ifdef _WIN32
#endif



udpClient udpClientStart(const char* ip, int port) {
    udpClient tmp;
#ifdef _WIN32
    // start up winsock
    WSADATA data;
    WORD version = MAKEWORD(2, 2);
    int wsOK = WSAStartup(version, &data);
    if (wsOK != 0) {
        cout << "Cant start winsock " << wsOK << "\n";
        return tmp;
    }
#endif
    tmp.server.sin_family = AF_INET;
    tmp.server.sin_port = htons(port);//htons converts to big endian
    inet_pton(AF_INET, ip, &tmp.server.sin_addr);
    tmp.out = socket(AF_INET, SOCK_DGRAM, 0);
    return tmp;
}

void udpClientCleanup(udpClient tmp) {
#ifdef _WIN32
    WSACleanup();
#endif
    //close socket
    closesocket(tmp.out);
}

void udpSend(const char* data, size_t len, udpClient tmp) {

    //write out socket
    int sendOK = sendto(tmp.out, data, len, 0, (sockaddr*)&tmp.server, sizeof(tmp.server));

    return;
}