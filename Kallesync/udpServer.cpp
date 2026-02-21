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
std::string IP;

udpServer udpServerStart(const char* ip, uint16_t port, uint32_t buffersize) {
    udpServer out;
#ifdef _WIN32
    // start up winsock
    WSADATA data;
    WORD version = MAKEWORD(2, 2);
    int wsOK = WSAStartup(version, &data);
    if (wsOK != 0) {
        cout << "Cant start winsock " << wsOK << "\n";
        return out;
    }
#endif
    //bind socket to ip and port
    out.in = socket(AF_INET, SOCK_DGRAM, 0);
    //out.serverHint;
    if (strcmp(ip, "any") == 0) {
        out.serverHint.sin_addr.s_addr = INADDR_ANY;
    }
    else {
        inet_pton(AF_INET, ip, &out.serverHint.sin_addr);
    }
    out.serverHint.sin_family = AF_INET;
    out.serverHint.sin_port = htons(port);//htons converts to big endian

    if (bind(out.in, (sockaddr*)&out.serverHint, sizeof(out.serverHint)) == SOCKET_ERROR) {
        std::cout << "Can't bind Socket: " << WSAGetLastError() << endl;
        return out;
    }
    //out.client;
    out.clientLength = sizeof(out.client);
    ZeroMemory(&out.client, out.clientLength);
    ZeroMemory(out.buf, buffersize);//clear buffer for new msg

    out.buffersize = buffersize;
    out.ip = ip;
    out.port = port;
    IP = ip;


    return out;
}

void udpServerCleanUp(udpServer& input) {
    closesocket(input.in);
#ifdef _WIN32
    WSACleanup();
#endif
}

bool udpServerGet(udpServer& input) {
    input.clientLength = sizeof(input.client);
    //enter server loop
    ZeroMemory(input.buf, input.buffersize);//clear buffer for new msg

    //wait for message
    int bytesIn = recvfrom(input.in, input.buf, input.buffersize, 0, (sockaddr*)&input.client, &input.clientLength);//grabs message and stores client
    if (bytesIn == -1) {
#ifdef _WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
            return false;  // No data available, not an error
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return false;  // No data available, not an error
        }
#endif
        cout << "error receiving from client " << WSAGetLastError() << endl;
        return false;
    }

    //display msg and client info
    //input.clientIP[256];
    ZeroMemory(&input.clientIP, 256);

    inet_ntop(AF_INET, &input.client.sin_addr, input.clientIP, 256);//gets client ip
    if (strcmp(IP.c_str(), "any") != 0 && strcmp(input.clientIP, IP.c_str()) != 0) {
        return false;
    }
    input.bytesReceived = bytesIn;
    return true;
}


/*/


g++ -std=c++17 mouseSync.cpp -o mouseSync



*/